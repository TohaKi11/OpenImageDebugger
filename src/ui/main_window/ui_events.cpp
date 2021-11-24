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

#include <QFileDialog>

#include "main_window.h"

#include "io/buffer_exporter.h"
#include "ui_main_window.h"
#include "visualization/components/camera.h"
#include "visualization/game_object.h"
#include "logger/logger.h"


using namespace std;


void MainWindow::resize_callback(int w, int h)
{
    for (auto& stage : stages_)
        stage.second->resize_callback(w, h);

    go_to_widget_->move(ui_->bufferPreview->width() - go_to_widget_->width(),
                        ui_->bufferPreview->height() - go_to_widget_->height());
}


void MainWindow::scroll_callback(float delta)
{
    if (link_views_enabled_) {
        for (auto& stage : stages_) {
            stage.second->scroll_callback(delta);
        }
    } else if (currently_selected_stage_ != nullptr) {
        currently_selected_stage_->scroll_callback(delta);
    }

    update_status_bar();

#if defined(Q_OS_DARWIN)
    ui_->bufferPreview->update();
#endif
    request_render_update_ = true;
}


void MainWindow::mouse_drag_event(int mouse_x, int mouse_y)
{
    const QPoint virtual_motion(static_cast<int>(mouse_x),
                                static_cast<int>(mouse_y));

    if (link_views_enabled_) {
        for (auto& stage : stages_)
            stage.second->mouse_drag_event(virtual_motion.x(),
                                           virtual_motion.y());
    } else if (currently_selected_stage_ != nullptr) {
        currently_selected_stage_->mouse_drag_event(virtual_motion.x(),
                                                    virtual_motion.y());
    }

    request_render_update_ = true;
}


void MainWindow::mouse_move_event(int, int)
{
    update_status_bar();
}


void MainWindow::resizeEvent(QResizeEvent*)
{
    persist_settings_deferred();
}


void MainWindow::moveEvent(QMoveEvent*)
{
    persist_settings_deferred();
}


void MainWindow::closeEvent(QCloseEvent*)
{
    is_window_ready_ = false;
    persist_settings_deferred();
}


bool MainWindow::eventFilter(QObject* target, QEvent* event)
{
    KeyboardState::update_keyboard_state(event);

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* key_event = static_cast<QKeyEvent*>(event);

        EventProcessCode event_intercepted = EventProcessCode::IGNORED;

        if (link_views_enabled_) {
            for (auto& stage : stages_) {
                EventProcessCode event_intercepted_stage =
                    stage.second->key_press_event(key_event->key());

                if (event_intercepted_stage == EventProcessCode::INTERCEPTED) {
                    event_intercepted = EventProcessCode::INTERCEPTED;
                }
            }
        } else if (currently_selected_stage_ != nullptr) {
            event_intercepted =
                currently_selected_stage_->key_press_event(key_event->key());
        }

        if (event_intercepted == EventProcessCode::INTERCEPTED) {
            request_render_update_ = true;
            update_status_bar();

            event->accept();
            return true;
        } else {
            return QObject::eventFilter(target, event);
        }
    }

    return false;
}


void MainWindow::recenter_buffer()
{
    if (link_views_enabled_) {
        for (auto& stage : stages_) {
            GameObject* cam_obj = stage.second->get_game_object("camera");
            Camera* cam = cam_obj->get_component<Camera>("camera_component");
            cam->recenter_camera();
        }
    } else {
        if (currently_selected_stage_ != nullptr) {
            GameObject* cam_obj =
                currently_selected_stage_->get_game_object("camera");
            Camera* cam = cam_obj->get_component<Camera>("camera_component");
            cam->recenter_camera();
        }
    }

    request_render_update_ = true;
}


void MainWindow::link_views_toggle()
{
    link_views_enabled_ = !link_views_enabled_;
}


void MainWindow::rotate_90_cw()
{
    const auto request_90_cw_rotation = [](Stage* stage) {
        GameObject* buffer_obj = stage->get_game_object("buffer");
        Buffer* buffer_comp =
            buffer_obj->get_component<Buffer>("buffer_component");

        buffer_comp->rotate(static_cast<float>(90.0 * M_PI / 180.0));
    };

    if (link_views_enabled_) {
        for (auto& stage : stages_) {
            request_90_cw_rotation(stage.second.get());
        }
    } else {
        if (currently_selected_stage_ != nullptr) {
            request_90_cw_rotation(currently_selected_stage_);
        }
    }

    request_render_update_ = true;
}


void MainWindow::rotate_90_ccw()
{
    const auto request_90_ccw_rotation = [](Stage* stage) {
        GameObject* buffer_obj = stage->get_game_object("buffer");
        Buffer* buffer_comp =
            buffer_obj->get_component<Buffer>("buffer_component");

        buffer_comp->rotate(static_cast<float>(-90.0 * M_PI / 180.0));
    };

    if (link_views_enabled_) {
        for (auto& stage : stages_) {
            request_90_ccw_rotation(stage.second.get());
        }
    } else {
        if (currently_selected_stage_ != nullptr) {
            request_90_ccw_rotation(currently_selected_stage_);
        }
    }

    request_render_update_ = true;
}


void MainWindow::image_list_tab_selected()
{
    bool is_item_selected = false;
    foreach (ListType list_type, get_all_list_types()) {

        QListWidget* list_widget = get_list_widget(list_type);
        if (list_widget == nullptr)
            continue;

        // Skip list it its tab isn't selected.
        if (!list_widget->isVisible())
            continue;

        for (int index_item = 0; index_item < list_widget->count(); ++index_item) {

            QListWidgetItem* item = list_widget->item(index_item);
            if (item == nullptr)
                continue;

            const bool is_selected = list_widget->currentItem() == item;
            const std::string symbol_value_item_str = item->data(Qt::UserRole).toString().toStdString();

            // Preview selected symbol.
            if (is_selected) {

                is_item_selected = true;
                item->setSelected(true);
                image_list_item_selected(item);
            }

            // Request buffer data from debugger bridge if symbol isn't synchronized yet
            if (loaded_vars_.count(symbol_value_item_str) == 0)
                request_plot_buffer(symbol_value_item_str);
        }
    }

    // If no items are selected - close preview.
    if (!is_item_selected)
        image_list_item_selected(nullptr);
}


void MainWindow::image_list_item_selected(QListWidgetItem* item)
{
    string symbol_name_str;
    if (item != nullptr)
        symbol_name_str = item->data(Qt::UserRole).toString().toStdString();

    auto stage = stages_.find(symbol_name_str);
    if (stage != stages_.end())
        set_currently_selected_stage(stage->second.get());
    else
        set_currently_selected_stage(nullptr);

    reset_ac_min_labels();
    reset_ac_max_labels();

    update_status_bar();
}


void MainWindow::remove_selected_watch_list_item()
{
    QListWidgetItem* item = ui_->imageList_watch->currentItem();
    if (item == nullptr)
        return;

    const string symbol_name_str = item->data(Qt::UserRole).toString().toStdString();
    if (symbol_name_str.empty())
        return;

    remove_image_list_item(ListType::Watch, symbol_name_str);
}


void MainWindow::symbol_selected()
{
    symbol_completed(ui_->symbolList->text());
}


void MainWindow::symbol_completed(QString symbol_name_str)
{
    const std::string symbol_name_stdstr = symbol_name_str.toStdString();
    if (symbol_name_stdstr.empty())
        return;

    // Request buffer data from debugger bridge
    request_plot_buffer(symbol_name_stdstr);

    // Clear symbol input
    ui_->symbolList->setText("");
    ui_->symbolList->clearFocus();

    // Construct a new list item if needed
    QListWidgetItem* item = find_image_list_item(ListType::Watch, symbol_name_stdstr);
    if (item == nullptr)
        item = add_image_list_item(ListType::Watch, symbol_name_stdstr);

    // Select newly created item.
    item->listWidget()->setFocus();
    item->listWidget()->setCurrentItem(item);
}


void MainWindow::remove_watch_list_item_action()
{
    auto sender_action = qobject_cast<QAction*>(sender());
    if (sender_action == nullptr)
        return;

    const std::string symbol_name_str = sender_action->data().toString().toStdString();
    if (symbol_name_str.empty())
        return;

    remove_image_list_item(ListType::Watch, symbol_name_str);
}


void MainWindow::export_buffer_action()
{
    auto sender_action = qobject_cast<QAction*>(sender());
    if (sender_action == nullptr)
        return;

    const std::string symbol_name_str = sender_action->data().toString().toStdString();
    if (symbol_name_str.empty())
        return;

    auto it_stage = stages_.find(symbol_name_str);
    if (it_stage == stages_.end())
        return;

    auto stage = it_stage->second;
    if (stage == nullptr)
        return;

    GameObject* buffer_obj = stage->get_game_object("buffer");
    Buffer* component = buffer_obj->get_component<Buffer>("buffer_component");

    QFileDialog file_dialog(this);
    file_dialog.setAcceptMode(QFileDialog::AcceptSave);
    file_dialog.setFileMode(QFileDialog::AnyFile);

    QHash<QString, BufferExporter::OutputType> output_extensions;
    output_extensions[tr("Image File (*.png)")] =
        BufferExporter::OutputType::Bitmap;
    output_extensions[tr("Octave Raw Matrix (*.oct)")] =
        BufferExporter::OutputType::OctaveMatrix;

    // Generate the save suffix string
    QHashIterator<QString, BufferExporter::OutputType> it(output_extensions);

    QString save_message;

    while (it.hasNext()) {
        it.next();
        save_message += it.key();
        if (it.hasNext())
            save_message += ";;";
    }

    file_dialog.setNameFilter(save_message);
    file_dialog.selectNameFilter(default_export_suffix_);

    if (file_dialog.exec() != QDialog::Accepted)
        return;

    const QStringList list_selected_files = file_dialog.selectedFiles();
    if (list_selected_files.isEmpty())
        return;

    const string file_name = list_selected_files.front().toStdString();
    const auto selected_filter = file_dialog.selectedNameFilter();

    // Export buffer
    BufferExporter::export_buffer(
                component,
                file_name,
                output_extensions[selected_filter]);

    // Update default export suffix to the previously used suffix
    default_export_suffix_ = selected_filter;

    // Persist settings
    persist_settings_deferred();
}


void MainWindow::show_context_menu(ListType type, const QPoint& pos)
{
    QListWidget* list_widget = get_list_widget(type);
    if (list_widget == nullptr)
        return;

    // Get item at desired position.
    QListWidgetItem* item = list_widget->itemAt(pos);
    if (item == nullptr)
        return;

    // Get name of buffer assigned to a specific item.
    const QString symbol_name_str = item->data(Qt::UserRole).toString();
    if (symbol_name_str.isEmpty())
        return;

    // Create menu and insert context actions
    QMenu myMenu(this);

    if (type == ListType::Watch) {

        QAction* removeAction = myMenu.addAction("Remove", this, &MainWindow::remove_watch_list_item_action);
        removeAction->setData(symbol_name_str);
    }

    QAction* exportAction = myMenu.addAction("Export buffer", this, &MainWindow::export_buffer_action);
    exportAction->setData(symbol_name_str);

    // Show context menu at handling position
    QPoint globalPos = list_widget->mapToGlobal(pos);
    myMenu.exec(globalPos);
}


void MainWindow::show_context_menu_locals(const QPoint& pos)
{
    show_context_menu(ListType::Locals, pos);
}


void MainWindow::show_context_menu_watch(const QPoint& pos)
{
    show_context_menu(ListType::Watch, pos);
}


void MainWindow::toggle_go_to_dialog()
{
    if (!go_to_widget_->isVisible()) {
        vec4 default_goal(0, 0, 0, 0);

        if (currently_selected_stage_ != nullptr) {
            GameObject* cam_obj =
                currently_selected_stage_->get_game_object("camera");
            Camera* cam = cam_obj->get_component<Camera>("camera_component");

            default_goal = cam->get_position();
        }

        go_to_widget_->set_defaults(default_goal.x(), default_goal.y());
    }

    go_to_widget_->toggle_visible();
}


void MainWindow::go_to_pixel(float x, float y)
{
    if (link_views_enabled_) {
        for (auto& stage : stages_) {
            stage.second->go_to_pixel(x, y);
        }
    } else if (currently_selected_stage_ != nullptr) {
        currently_selected_stage_->go_to_pixel(x, y);
    }

    update_status_bar();

    request_render_update_ = true;
}
