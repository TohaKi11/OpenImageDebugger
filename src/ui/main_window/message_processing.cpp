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


void MainWindow::add_new_local_symbols()
{
    foreach (const auto& symbol_value_str, available_vars_) {

        const std::string symbol_value_stdstr = symbol_value_str.toStdString();
        const bool symbol_contains_dot = symbol_value_str.contains('.');
        const bool symbol_contains_arrow = symbol_value_str.contains("->");
        const bool is_local_symbol = !(symbol_contains_dot || symbol_contains_arrow);

        if (!is_local_symbol)
            continue;

        // Construct a new item in locals list
        if (find_image_list_item(ListType::Locals, symbol_value_stdstr) == nullptr)
            add_image_list_item(ListType::Locals, symbol_value_stdstr);
    }
}


void MainWindow::remove_old_local_symbols()
{
    QStringList unavailable_local_vars;
    for (int index_item = 0; index_item < ui_->imageList_locals->count(); ++index_item) {

        QListWidgetItem* item = ui_->imageList_locals->item(index_item);
        if (item == nullptr)
            continue;

        const QString symbol_value_item_str = item->data(Qt::UserRole).toString();

        bool is_available = false;
        foreach (const auto& symbol_value_available_str, available_vars_) {

            if (symbol_value_item_str != symbol_value_available_str)
                continue;

            is_available = true;
            break;
        }

        if (!is_available)
            unavailable_local_vars.append(symbol_value_item_str);
    }

    foreach(const QString& symbol_value_str, unavailable_local_vars)
        remove_image_list_item(ListType::Locals, symbol_value_str.toStdString());
}


void MainWindow::decode_set_available_symbols()
{
    std::unique_lock<std::mutex> lock(ui_mutex_);
    MessageDecoder message_decoder(&socket_);
    message_decoder.read<QStringList, QString>(available_vars_);

    Logger::instance()->info("Received available symbols: {}", available_vars_.join(", ").toStdString());

    // Debugger just hit the breakpoint. All variables are not synced now.
    loaded_vars_.clear();

    // Add new local available items to the locals list.
    add_new_local_symbols();

    // Remove all items from locals list that became unavailable.
    remove_old_local_symbols();

    completer_updated_ = true;
}


void MainWindow::reset_image_lists_data()
{
    foreach (ListType list_type, get_all_list_types()) {

        QListWidget* list_widget = get_list_widget(list_type);
        if (list_widget == nullptr)
            continue;

        for (int index_item = 0; index_item < list_widget->count(); ++index_item) {

            QListWidgetItem* item = list_widget->item(index_item);
            if (item == nullptr)
                continue;

            const QString symbol_value_item_str = item->data(Qt::UserRole).toString();

            item->setText(symbol_value_item_str);
            item->setIcon(draw_image_list_icon_stub());
        }
    }
}


QStringList MainWindow::prepare_observed_symbols_list()
{
    QStringList observable_vars;
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
            const QString symbol_value_item_str = item->data(Qt::UserRole).toString();

            // Prioritize symbol which is selected (preview is shown).
            if (is_selected)
                observable_vars.prepend(symbol_value_item_str);
            else
                observable_vars.append(symbol_value_item_str);
        }
    }

    return observable_vars;
}


void MainWindow::respond_get_observed_symbols()
{
    Logger::instance()->info("Received request to provide observed symbols");

    // Reset text and icon of all lists items to visualize that they are loading.
    reset_image_lists_data();

    // Prepare a list of observable variables.
    QStringList observable_vars = prepare_observed_symbols_list();

    // Compose message.
    MessageComposer message_composer(&socket_);
    message_composer.push(MessageType::GetObservedSymbolsResponse)
        .push(static_cast<size_t>(observable_vars.size()));
    for (const auto& symbol_value_item_str : observable_vars)
        message_composer.push(symbol_value_item_str.toStdString());
    message_composer.send();

    Logger::instance()->info("Sent observed symbols: {}", observable_vars.join(", ").toStdString());
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

    // In case if item is selected - show it
    foreach (ListType list_type, get_all_list_types()) {

        QListWidget* list_widget = get_list_widget(list_type);
        if (list_widget == nullptr)
            continue;

        if (!list_widget->isVisible())
            continue;

        QListWidgetItem* item = find_image_list_item(list_type, variable_name_str);
        if (item == nullptr)
            continue;

        const bool is_selected = list_widget->currentItem() == item;
        if (is_selected)
            image_list_item_selected(item);
    }

    // Update icon and text of corresponding item in image list
    // TODO: Icon painting works incorrectly for small matrices.
    // Whole app window remains painted with the matrix colors.
    if ((buff_width * buff_height) > 100)
        repaint_image_list_icon(variable_name_str);
    update_image_list_label(variable_name_str, label_str);

    // Update AC values
    reset_ac_min_labels();
    reset_ac_max_labels();

    // This variable is synchronized now.
    loaded_vars_.insert(variable_name_str);

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
