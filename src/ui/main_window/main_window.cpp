/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2019 OpenImageDebugger contributors
 * (https://github.com/OpenImageDebugger/OpenImageDebugger)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <iomanip>

#include <QAction>
#include <QDateTime>
#include <QScreen>
#include <QSettings>
#include <QSplitter>

#include "main_window.h"

#include "ui_main_window.h"
#include "visualization/components/camera.h"
#include "visualization/game_object.h"
#include "logger/logger.h"


using namespace std;


Q_DECLARE_METATYPE(QList<QString>)


MainWindow::MainWindow(const ConnectionSettings& host_settings,
                       QWidget* parent)
    : QMainWindow(parent)
    , is_window_ready_(false)
    , request_render_update_(true)
    , request_icons_update_(true)
    , completer_updated_(false)
    , ac_enabled_(false)
    , link_views_enabled_(false)
    , icon_width_base_(100)
    , icon_height_base_(75)
    , currently_selected_stage_(nullptr)
    , ui_(new Ui::MainWindowUi)
    , host_settings_(host_settings)
{
    QCoreApplication::instance()->installEventFilter(this);

    ui_->setupUi(this);

    initialize_settings();
    initialize_ui_icons();
    initialize_ui_signals();
    initialize_timers();
    initialize_symbol_completer();
    initialize_left_pane();
    initialize_auto_contrast_form();
    initialize_toolbar();
    initialize_status_bar();
    initialize_visualization_pane();
    initialize_go_to_widget();
    initialize_shortcuts();
    initialize_networking();

    is_window_ready_ = true;
}


MainWindow::~MainWindow()
{
    held_buffers_.clear();
    is_window_ready_ = false;

    delete ui_;
}


void MainWindow::show()
{
    update_timer_.start(static_cast<int>(1000.0 / render_framerate_));
    QMainWindow::show();
}


void MainWindow::draw()
{
    if (currently_selected_stage_ != nullptr) {
        currently_selected_stage_->draw();
    }
}


GLCanvas* MainWindow::gl_canvas()
{
    return ui_->bufferPreview;
}


QSizeF MainWindow::get_icon_size()
{
    const qreal screen_dpi_scale = get_screen_dpi_scale();
    return QSizeF(icon_width_base_ * screen_dpi_scale,
                  icon_height_base_ * screen_dpi_scale);
}


bool MainWindow::is_window_ready()
{
    return ui_->bufferPreview->is_ready() && is_window_ready_;
}


void MainWindow::loop()
{
    decode_incoming_messages();

    if (completer_updated_) {
        // Update auto-complete suggestion list
        symbol_completer_->update_symbol_list(available_vars_);
        completer_updated_ = false;
    }

    // Run update for current stage
    if (currently_selected_stage_ != nullptr) {
        currently_selected_stage_->update();
    }

    // Update visualization pane
    if (request_render_update_) {
        ui_->bufferPreview->update();
        request_render_update_ = false;
    }

    // Update an icon of every entry in image list
    if (request_icons_update_) {

        for (auto& pair_stage : stages_)
            repaint_image_list_icon(pair_stage.first);

        request_icons_update_ = false;
    }
}


void MainWindow::request_render_update()
{
    request_render_update_ = true;
}


void MainWindow::request_icons_update()
{
    request_icons_update_ = true;
}


void MainWindow::persist_settings_previous_session(QSettings& settings)
{
    settings.beginGroup("PreviousSession");

    QStringList persisted_session_buffers;
    for (int index_item = 0; index_item < ui_->imageList_watch->count(); ++index_item) {

        QListWidgetItem* item = ui_->imageList_watch->item(index_item);
        if (item == nullptr)
            continue;

        const QString symbol_value_item_str = item->data(Qt::UserRole).toString();
        if (symbol_value_item_str.isEmpty())
            continue;

        persisted_session_buffers.append(symbol_value_item_str);
    }

    settings.setValue("buffers", persisted_session_buffers);

    settings.endGroup();
}


void MainWindow::persist_settings()
{
    QSettings settings(QSettings::Format::IniFormat,
                       QSettings::Scope::UserScope,
                       "OpenImageDebugger");

    // Write default suffix for buffer export
    settings.setValue("Export/default_export_suffix", default_export_suffix_);

    // Write maximum framerate
    settings.setValue("Rendering/maximum_framerate", render_framerate_);

    // Write previous session symbols
    persist_settings_previous_session(settings);

    // Write UI geometry.
    settings.beginGroup("UI");
    {
        const QList<int> listSizesInt = ui_->splitter->sizes();

        QList<QVariant> listSizesVariant;
        for (int size : listSizesInt)
            listSizesVariant.append(size);

        settings.setValue("splitter", listSizesVariant);
    }
    settings.setValue("minmax_visible", ui_->acEdit->isChecked());
    settings.setValue("contrast_enabled", ui_->acToggle->isChecked());
    settings.setValue("tab", ui_->tabWidget->currentIndex());
    settings.endGroup();

    // Write window position/size
    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("pos", pos());
    settings.endGroup();

    settings.sync();
}


std::pair<int, int> MainWindow::get_stage_coordinates(float pos_window_x, float pos_window_y)
{
    if (currently_selected_stage_ == nullptr)
        return std::make_pair(0, 0);

    GameObject* cam_obj = currently_selected_stage_->get_game_object("camera");
    Camera* cam         = cam_obj->get_component<Camera>("camera_component");

    GameObject* buffer_obj =
        currently_selected_stage_->get_game_object("buffer");
    Buffer* buffer = buffer_obj->get_component<Buffer>("buffer_component");

    float win_w = ui_->bufferPreview->width();
    float win_h = ui_->bufferPreview->height();
    vec4 mouse_pos_ndc(2.0f * (pos_window_x - win_w / 2) / win_w,
                       -2.0f * (pos_window_y - win_h / 2) / win_h,
                       0,
                       1);
    mat4 view      = cam_obj->get_pose().inv();
    mat4 buff_pose = buffer_obj->get_pose();
    mat4 vp_inv    = (cam->projection * view * buff_pose).inv();

    vec4 mouse_pos = vp_inv * mouse_pos_ndc;
    mouse_pos +=
        vec4(buffer->buffer_width_f / 2.f, buffer->buffer_height_f / 2.f, 0, 0);

    return std::make_pair(
                static_cast<int>(floor(mouse_pos.x())),
                static_cast<int>(floor(mouse_pos.y()))
                );
}


QListWidget* MainWindow::get_list_widget(ListType type)
{
    switch (type) {

    case ListType::Locals:
        return ui_->imageList_locals;
    case ListType::Watch:
        return ui_->imageList_watch;
    default:
        return nullptr;
    }
}


QString MainWindow::get_list_name(ListType type)
{
    switch (type) {

    case ListType::Locals:
        return "Locals";
    case ListType::Watch:
        return "Watch";
    default:
        return "Undefined";
    }
}


auto MainWindow::get_all_list_types() -> QList<ListType>
{
    return {

        ListType::Locals,
        ListType::Watch
    };
}


void MainWindow::update_status_bar()
{
    if (currently_selected_stage_ == nullptr) {

        status_bar_->clear();
        return;
    }

    stringstream message;

    GameObject* cam_obj =
            currently_selected_stage_->get_game_object("camera");
    Camera* cam = cam_obj->get_component<Camera>("camera_component");

    GameObject* buffer_obj =
            currently_selected_stage_->get_game_object("buffer");
    Buffer* buffer = buffer_obj->get_component<Buffer>("buffer_component");

    float mouse_x = ui_->bufferPreview->mouse_x();
    float mouse_y = ui_->bufferPreview->mouse_y();

    const std::pair<int, int> mouse_pos = get_stage_coordinates(mouse_x, mouse_y);

    message << std::fixed << std::setprecision(5) << "("
            << mouse_pos.first << ", "
            << mouse_pos.second << ")\t"
            << cam->compute_zoom() * 100.0f << "%";
    message << " val=";

    buffer->get_pixel_info(
                message,
                mouse_pos.first,
                mouse_pos.second);

    status_bar_->setText(message.str().c_str());
}


qreal MainWindow::get_screen_dpi_scale()
{
    return QGuiApplication::primaryScreen()->devicePixelRatio();
}


string MainWindow::get_type_label(BufferType type, int channels)
{
    stringstream result;
    if (type == BufferType::Float32) {
        result << "float32";
    } else if (type == BufferType::UnsignedByte) {
        result << "uint8";
    } else if (type == BufferType::Short) {
        result << "int16";
    } else if (type == BufferType::UnsignedShort) {
        result << "uint16";
    } else if (type == BufferType::Int32) {
        result << "int32";
    } else if (type == BufferType::Float64) {
        result << "float64";
    }
    result << "x" << channels;

    return result.str();
}


void MainWindow::persist_settings_deferred()
{
    settings_persist_timer_.start(100);
}


void MainWindow::erase_stage(const std::string& symbol_name_str)
{
    auto it_stage = stages_.find(symbol_name_str);
    if (it_stage != stages_.end()) {

        std::shared_ptr<Stage> stage_shr_ptr = it_stage->second;
        if (currently_selected_stage_ && stage_shr_ptr && (currently_selected_stage_.get() == stage_shr_ptr.get()))
            reset_currently_selected_stage();

        stages_.erase(it_stage);
    }

    held_buffers_.erase(symbol_name_str);
}


void MainWindow::set_currently_selected_stage(const std::shared_ptr<Stage>& stage)
{
    currently_selected_stage_ = stage;
    request_render_update_    = true;
}

void MainWindow::reset_currently_selected_stage()
{
    currently_selected_stage_.reset();
    request_render_update_    = true;
}


QListWidgetItem* MainWindow::add_image_list_item(ListType type, const std::string& variable_name_str)
{
    QListWidget* list_widget = get_list_widget(type);
    if (list_widget == nullptr)
        return nullptr;

    // Construct an icon stub.
    const QPixmap buffer_pixmap = draw_image_list_icon_stub();

    // Construct a new list widget item.
    QListWidgetItem* item = new QListWidgetItem(buffer_pixmap, variable_name_str.c_str(), list_widget);
    item->setData(Qt::UserRole, QString(variable_name_str.c_str()));
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                   Qt::ItemIsDragEnabled);
    list_widget->addItem(item);

    Logger::instance()->info("Added symbol {} to the {} list", variable_name_str, get_list_name(type).toStdString());

    // Update previous session settings in case of a watch list item adding.
    if (type == ListType::Watch)
        persist_settings_deferred();

    return item;
}


void MainWindow::remove_image_list_item(ListType type, const std::string& symbol_name_str)
{
    if (symbol_name_str.empty())
        return;

    // Remove item from selected list.
    QListWidgetItem* item = find_image_list_item(type, symbol_name_str);
    if (item != nullptr)
        delete item;

    Logger::instance()->info("Removed symbol {} from the {} list", symbol_name_str, get_list_name(type).toStdString());

    // Remove stage object if there no other links to the buffer in any list.
    if (is_list_item_exists(symbol_name_str))
        erase_stage(symbol_name_str);

    // It this was the last item in the list.
    if (get_list_widget(type)->count() == 0 || stages_.size() == 0)
        reset_currently_selected_stage();

    // Update previous session settings in case of a watch list item deletion.
    if (type == ListType::Watch)
        persist_settings_deferred();
}


QListWidgetItem* MainWindow::find_image_list_item(ListType type, const std::string& variable_name_str)
{
    QListWidget* list_widget = get_list_widget(type);
    if (list_widget == nullptr)
        return nullptr;

    // Looking for corresponding item...
    for (int i = 0; i < list_widget->count(); ++i) {

        QListWidgetItem* item = list_widget->item(i);
        if (item == nullptr)
            continue;

        const string current_variable_name_str = item->data(Qt::UserRole).toString().toStdString();
        if (current_variable_name_str.empty())
            continue;

        if (current_variable_name_str != variable_name_str)
            continue;

        return item;
    }

    return nullptr;
}


bool MainWindow::is_list_item_exists(const std::string& variable_name_str)
{
    foreach (ListType type, get_all_list_types()) {

        if (find_image_list_item(type, variable_name_str) != nullptr)
            return true;
    }

    return false;
}


QPixmap MainWindow::draw_image_list_icon(const std::shared_ptr<Stage>& stage)
{
    // Buffer icon dimensions
    const QSizeF icon_size   = get_icon_size();
    const int icon_width     = static_cast<int>(icon_size.width());
    const int icon_height    = static_cast<int>(icon_size.height());
    const int bytes_per_line = icon_width * 3;

    if (!stage)
        return draw_image_list_icon_stub();

    // Update buffer icon
    ui_->bufferPreview->render_buffer_icon(
                stage.get(), icon_width, icon_height);

    // Construct icon widget
    QImage buffer_image_preview(stage->buffer_icon.data(),
                              icon_width,
                              icon_height,
                              bytes_per_line,
                              QImage::Format_RGB888);

    return QPixmap::fromImage(buffer_image_preview);
}


QPixmap MainWindow::draw_image_list_icon_stub()
{
    // Buffer icon dimensions
    const QSizeF icon_size   = get_icon_size();
    const int icon_width     = static_cast<int>(icon_size.width());
    const int icon_height    = static_cast<int>(icon_size.height());

    // Construct stub pixmap.
    QPixmap buffer_pixmap(icon_width, icon_height);
    buffer_pixmap.fill(Qt::lightGray);
    return buffer_pixmap;
}


void MainWindow::repaint_image_list_icon(const std::string& variable_name_str)
{
    QPixmap bufferPixmap;

    // Try to find stage of a corresponding variable.
    auto itStage = stages_.find(variable_name_str);
    if (itStage != stages_.end())
        bufferPixmap = draw_image_list_icon(itStage->second);
    else
        bufferPixmap = draw_image_list_icon_stub();

    // Replace icon in the corresponding item
    foreach (ListType list_type, get_all_list_types()) {

        QListWidgetItem* item = find_image_list_item(list_type, variable_name_str);
        if (item == nullptr)
            continue;

        item->setIcon(bufferPixmap);
    }
}


string chop_first_line(const std::string& str)
{
    const size_t pos = str.find_first_of("\r\n");
    if (pos == std::string::npos)
        return str;

    return str.substr(0, pos);
}


string MainWindow::construct_image_list_label(const std::string& display_name_str,
                                  int visualized_width, int visualized_height,
                                  BufferType buff_type, int buff_channels)
{
    stringstream label_ss;
    label_ss << chop_first_line(display_name_str);
    label_ss << "\n[" << visualized_width << "x" << visualized_height << "]";
    label_ss << "\n" << get_type_label(buff_type, buff_channels);
    return label_ss.str();
}


string MainWindow::construct_image_list_label(const std::string& display_name_str,
                                  const std::string& status_str)
{
    stringstream label_ss;
    label_ss << chop_first_line(display_name_str);
    label_ss << "\n" << chop_first_line(status_str);
    label_ss << "\n";
    return label_ss.str();
}


void MainWindow::update_image_list_label(const std::string& variable_name_str, const std::string& label_str)
{
    QString label_qstr = QString::fromStdString(label_str);

    // Replace text in the corresponding item
    foreach (ListType list_type, get_all_list_types()) {

        QListWidgetItem* item = find_image_list_item(list_type, variable_name_str);
        if (item == nullptr)
            continue;

        item->setText(label_qstr);
    }
}
