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

#include "main_window.h"
#include "ipc/message_composer.h"
#include "ipc/message_decoder.h"
#include "logger/logger.h"

#include "ui_main_window.h"

using namespace std;


void MainWindow::decode_set_available_symbols()
{
    std::unique_lock<std::mutex> lock(ui_mutex_);
    MessageDecoder message_decoder(&socket_);
    message_decoder.read<QStringList, QString>(available_vars_);

    Logger::instance()->info("Received available symbols: {}", available_vars_.join(", ").toStdString());

    foreach (const auto& symbol_value_str, available_vars_) {

        // Plot buffer if it was available in the previous session
        const std::string symbol_value_stdstr = symbol_value_str.toStdString();
        if (previous_session_buffers_.find(symbol_value_stdstr) == previous_session_buffers_.end())
            continue;

        request_plot_buffer(symbol_value_stdstr);

        // Construct a new list widget if needed
        QListWidgetItem* item = find_image_list_item(symbol_value_stdstr);
        if (item == nullptr)
            item = add_image_list_item(symbol_value_stdstr);
    }

    // Clean previous session buffers in order to request them only once at app startup.
    previous_session_buffers_.clear();

    completer_updated_ = true;
}


void MainWindow::respond_get_observed_symbols()
{
    Logger::instance()->info("Received request to provide observed symbols");

    MessageComposer message_composer(&socket_);
    message_composer.push(MessageType::GetObservedSymbolsResponse)
        .push(held_buffers_.size());
    for (const auto& name : held_buffers_)
        message_composer.push(name.first);
    message_composer.send();

    {
        std::stringstream ss;
        bool isFirst = true;
        for (const auto& name : held_buffers_) {

            if (isFirst)
                isFirst = false;
            else
                ss << ", ";

            ss << name.first;
        }
        Logger::instance()->info("Sent observed symbols: {}", ss.str());
    }
}


QListWidgetItem* MainWindow::add_image_list_item(const std::string& variable_name_str)
{
    // Construct an icon stub.
    const QPixmap buffer_pixmap = draw_image_list_icon_stub();

    // Construct a new list widget item.
    QListWidgetItem* item = new QListWidgetItem(buffer_pixmap, variable_name_str.c_str(), ui_->imageList);
    item->setData(Qt::UserRole, QString(variable_name_str.c_str()));
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                   Qt::ItemIsDragEnabled);
    ui_->imageList->addItem(item);

    return item;
}


QListWidgetItem* MainWindow::find_image_list_item(const std::string& variable_name_str)
{
    // Looking for corresponding item...
    for (int i = 0; i < ui_->imageList->count(); ++i) {

        QListWidgetItem* item = ui_->imageList->item(i);
        if (item->data(Qt::UserRole) != variable_name_str.c_str())
            continue;

        return item;
    }

    return nullptr;
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
    QListWidgetItem* item = find_image_list_item(variable_name_str);
    if (item == nullptr)
        return;

    item->setIcon(bufferPixmap);
}


void MainWindow::update_image_list_label(const std::string& variable_name_str, const std::string& label_str)
{
    // Replace text in the corresponding item
    QListWidgetItem* item = find_image_list_item(variable_name_str);
    if (item == nullptr)
        return;

    item->setText(label_str.c_str());
}


void MainWindow::decode_plot_buffer_contents()
{
    // Read buffer info
    string variable_name_str;
    string display_name_str;
    string pixel_layout_str;
    bool transpose_buffer;
    int buff_width;
    int buff_height;
    int buff_channels;
    int buff_stride;
    BufferType buff_type;
    vector<uint8_t> buff_contents;

    MessageDecoder message_decoder(&socket_);
    message_decoder.read(variable_name_str)
        .read(display_name_str)
        .read(pixel_layout_str)
        .read(transpose_buffer)
        .read(buff_width)
        .read(buff_height)
        .read(buff_channels)
        .read(buff_stride)
        .read(buff_type)
        .read(buff_contents);

    Logger::instance()->info("Received symbol data: {}", display_name_str);

    // Put the data buffer into the container
    if (buff_type == BufferType::Float64) {
        held_buffers_[variable_name_str] =
            make_float_buffer_from_double(buff_contents);
    } else {
        held_buffers_[variable_name_str] = std::move(buff_contents);
    }
    const uint8_t* buff_ptr = held_buffers_[variable_name_str].data();

    // Human readable dimensions
    int visualized_width;
    int visualized_height;
    if (!transpose_buffer) {
        visualized_width  = buff_width;
        visualized_height = buff_height;
    } else {
        visualized_width  = buff_height;
        visualized_height = buff_width;
    }

    string label_str;
    {
        stringstream label_ss;
        label_ss << display_name_str;
        label_ss << "\n[" << visualized_width << "x" << visualized_height << "]";
        label_ss << "\n" << get_type_label(buff_type, buff_channels);
        label_str = label_ss.str();
    }

    // Find corresponding stage buffer
    auto buffer_stage = stages_.find(variable_name_str);
    if (buffer_stage == stages_.end()) {

        // Construct a new stage buffer if needed
        shared_ptr<Stage> stage = make_shared<Stage>(this);
        if (!stage->initialize()) {
            cerr << "[error] Could not initialize opengl canvas!" << endl;
        }
        stage->contrast_enabled    = ac_enabled_;
        buffer_stage = stages_.emplace(variable_name_str, stage).first;
    }

    // Update buffer data
    buffer_stage->second->buffer_update(
                buff_ptr,
                buff_width,
                buff_height,
                buff_channels,
                buff_type,
                buff_stride,
                pixel_layout_str,
                transpose_buffer);

    // Construct a new list widget if needed
    QListWidgetItem* item = find_image_list_item(variable_name_str);
    if (item == nullptr)
        item = add_image_list_item(variable_name_str);

    // Update icon and text of corresponding item in image list
    repaint_image_list_icon(variable_name_str);
    update_image_list_label(variable_name_str, label_str);

    // Update AC values
    if (currently_selected_stage_ != nullptr) {
        reset_ac_min_labels();
        reset_ac_max_labels();
    }

    // Update list of observed symbols in settings
    persist_settings_deferred();

    request_render_update_ = true;
}


void MainWindow::decode_incoming_messages()
{
    // Close application if server has disconnected
    if(socket_.state() == QTcpSocket::UnconnectedState) {
        QApplication::quit();
    }

    available_vars_.clear();

    if (socket_.bytesAvailable() == 0) {
        return;
    }

    MessageType header;
    if (!socket_.read(reinterpret_cast<char*>(&header),
                      static_cast<qint64>(sizeof(header)))) {
        return;
    }

    socket_.waitForReadyRead(100);

    switch (header) {
    case MessageType::SetAvailableSymbols:
        decode_set_available_symbols();
        break;
    case MessageType::GetObservedSymbols:
        respond_get_observed_symbols();
        break;
    case MessageType::PlotBufferContents:
        decode_plot_buffer_contents();
        break;
    default:
        Logger::instance()->info("Received undefined command");
        break;
    }
}


void MainWindow::request_plot_buffer(const std::string& buffer_name_str)
{
    MessageComposer message_composer(&socket_);
    message_composer.push(MessageType::PlotBufferRequest)
        .push(buffer_name_str)
        .send();

    Logger::instance()->info("Sent request to provide symbol data: {}", buffer_name_str);
}
