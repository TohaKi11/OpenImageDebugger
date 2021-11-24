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

#include <queue>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>

#include "debuggerinterface/preprocessor_directives.h"
#include "debuggerinterface/python_native_interface.h"
#include "oid_bridge.h"
#include "ipc/message_composer.h"
#include "ipc/message_decoder.h"
#include "system/process/process.h"
#include "logger/logger.h"

#include <QDataStream>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>


using namespace std;

struct UiMessage
{
    virtual ~UiMessage();

    virtual bool isSame(const UiMessage& other) const = 0;
};

UiMessage::~UiMessage()
{
}

struct GetObservedSymbolsResponseMessage : public UiMessage
{
    std::deque<string> observed_symbols;
    ~GetObservedSymbolsResponseMessage();

    virtual bool isSame(const UiMessage& other) const ;
};

GetObservedSymbolsResponseMessage::~GetObservedSymbolsResponseMessage()
{
}

bool GetObservedSymbolsResponseMessage::isSame(const UiMessage& other) const
{
    try {

        const auto& other_casted = dynamic_cast<const GetObservedSymbolsResponseMessage&>(other);

        return observed_symbols == other_casted.observed_symbols;

    }  catch (const std::bad_cast&) {

        return false;
    }
}

struct PlotBufferRequestMessage : public UiMessage
{
    std::string buffer_name;
    ~PlotBufferRequestMessage();

    virtual bool isSame(const UiMessage& other) const ;
};

PlotBufferRequestMessage::~PlotBufferRequestMessage()
{
}

bool PlotBufferRequestMessage::isSame(const UiMessage& other) const
{
    try {

        const auto& other_casted = dynamic_cast<const PlotBufferRequestMessage&>(other);

        return buffer_name == other_casted.buffer_name;

    }  catch (const std::bad_cast&) {

        return false;
    }
}

class PyGILRAII
{
  public:
    PyGILRAII()
    {
        _py_gil_state = PyGILState_Ensure();
    }
    PyGILRAII(const PyGILRAII&)  = delete;
    PyGILRAII(const PyGILRAII&&) = delete;

    PyGILRAII& operator=(const PyGILRAII&) = delete;
    PyGILRAII& operator=(const PyGILRAII&&) = delete;

    ~PyGILRAII()
    {
        PyGILState_Release(_py_gil_state);
    }

  private:
    PyGILState_STATE _py_gil_state;
};

class OidBridge
{
  public:
    OidBridge(int (*plot_callback)(const char*))
        : ui_proc_{}
        , client_{nullptr}
        , plot_callback_{plot_callback}
    {
    }

    bool start()
    {
        const static quint16 serverPort =
#if !defined(IS_DEVELOPMENT)
            0;      // A port is chosen automatically.
#else
            9588;   // A port is statically set for convenient OID development.
#endif

        // Initialize server
        if (!server_.listen(QHostAddress::Any, serverPort)) {

            Logger::instance()->error("Could not start TCP server");
            return false;
        }

        Logger::instance()->info("Waiting for connection to port {}", server_.serverPort());
        Logger::instance()->flush();

        const string window_binary_path = this->oid_path_ + "/oidwindow";
        const string port_str = std::to_string(server_.serverPort());
        const string logger_file_name = Logger::get_file_name();

        const vector<string> command {
            window_binary_path,
            "-style", "fusion",
            "-p", port_str,
            "-l", logger_file_name
        };

        // Don't run UI application in case of OID development.
#if !defined(IS_DEVELOPMENT)
        ui_proc_.start(command);
        ui_proc_.waitForStart();
#endif

        Logger::instance()->info("UI app started");

        wait_for_client();

        return client_ != nullptr;
    }

    void set_path(const string& oid_path)
    {
        oid_path_ = oid_path;
    }

    bool is_window_ready()
    {
        if (client_ == nullptr)
            return false;

#if !defined(IS_DEVELOPMENT)
        if (!ui_proc_.isRunning())
            return false;
#endif

        return true;
    }

    void log_message(spdlog::level::level_enum level, const std::string& message_str)
    {
        Logger::instance()->log(level, message_str);
    }

    deque<string> get_observed_symbols()
    {
        assert(client_ != nullptr);

        MessageComposer message_composer(client_);
        message_composer.push(MessageType::GetObservedSymbols)
                .send();

        Logger::instance()->info("Sent request to provide observed symbols");

        auto response = fetch_message(MessageType::GetObservedSymbolsResponse);
        if (response != nullptr) {
            return static_cast<GetObservedSymbolsResponseMessage*>(
                       response.get())
                ->observed_symbols;
        } else {
            return {};
        }
    }


    void set_available_symbols(const deque<string>& available_vars)
    {
        assert(client_ != nullptr);

        MessageComposer message_composer(client_);
        message_composer.push(MessageType::SetAvailableSymbols)
            .push(available_vars)
            .send();

        {
            std::stringstream ss;
            bool isFirst = true;
            for (const auto& available_var : available_vars) {

                if (isFirst)
                    isFirst = false;
                else
                    ss << ", ";

                ss << available_var;
            }
            Logger::instance()->info("Sent available symbols: {}", ss.str());
        }
    }

    void run_event_loop()
    {
        try_read_incoming_messages(static_cast<int>(1000.0 / 5.0));

        unique_ptr<UiMessage> plot_request_message;
        while ((plot_request_message = try_get_stored_message(
                    MessageType::PlotBufferRequest)) != nullptr) {
            const PlotBufferRequestMessage* msg =
                dynamic_cast<PlotBufferRequestMessage*>(
                    plot_request_message.get());
            plot_callback_(msg->buffer_name.c_str());
        }
    }

    void plot_buffer(const string& variable_name_str,
                     const string& display_name_str,
                     const string& pixel_layout_str,
                     bool transpose_buffer,
                     int buff_width,
                     int buff_height,
                     int buff_channels,
                     int buff_stride,
                     BufferType buff_type,
                     uint8_t* buff_ptr,
                     size_t buff_length)
    {
        MessageComposer message_composer(client_);
        message_composer.push(MessageType::PlotBufferContents)
            .push(variable_name_str)
            .push(display_name_str)
            .push(pixel_layout_str)
            .push(transpose_buffer)
            .push(buff_width)
            .push(buff_height)
            .push(buff_channels)
            .push(buff_stride)
            .push(buff_type)
            .push(buff_ptr, buff_length)
            .send();

        Logger::instance()->info("Sent symbol data: {}", display_name_str);
    }

    ~OidBridge()
    {
#if !defined(IS_DEVELOPMENT)
        ui_proc_.kill();
#endif
    }

  private:
    Process ui_proc_;
    QTcpServer server_;
    QTcpSocket* client_;
    string oid_path_;

    int (*plot_callback_)(const char*);

    std::map<MessageType, std::list<std::unique_ptr<UiMessage>>> received_messages_;

private:
    std::unique_ptr<UiMessage>
    try_get_stored_message(const MessageType& msg_type)
    {
        // Find a queue of messages of specific type.
        auto it_messages = received_messages_.find(msg_type);
        if (it_messages == received_messages_.end())
            return nullptr;

        // Check that queue isn't empty.
        auto& queue_messages = it_messages->second;
        if (queue_messages.empty())
            return nullptr;

        // Take the olders message from queue (FIFO).
        unique_ptr<UiMessage> result = std::move(queue_messages.front());
        queue_messages.pop_front();
        return result;
    }


    static size_t get_queue_size_limit(MessageType header)
    {
        switch (header) {
        // Commands that represent the state of the system.
        // Only latest state should be used.
        case MessageType::GetObservedSymbolsResponse:
            return 1;
        // Repeatable commands. Every command matters.
        // Limit to avoid overflows.
        case MessageType::PlotBufferRequest:
        default:
            return 512;
        }
    }


    void try_read_incoming_messages(int msecs = 3000)
    {
        assert(client_ != nullptr);

        do {
            client_->waitForReadyRead(msecs);

            if (client_->bytesAvailable() == 0) {
                break;
            }

            // Read the header of the message.
            MessageType header;
            client_->read(reinterpret_cast<char*>(&header),
                          static_cast<qint64>(sizeof(header)));

            // Read the rest of the message depending on its type.
            unique_ptr<UiMessage> message;
            switch (header) {
            case MessageType::PlotBufferRequest:
                message = decode_plot_buffer_request();
                break;
            case MessageType::GetObservedSymbolsResponse:
                message = decode_get_observed_symbols_response();
                break;
            default:
                Logger::instance()->error("Received message with incorrect header");
                break;
            }

            // Put the message in the end of the queue.
            auto& queue_messages = received_messages_[header];

            // Remove duplicated messages.
            queue_messages.remove_if([&message](const std::unique_ptr<UiMessage>& other){

                return message->isSame(*other.get());
            });

            // Add new message to the queue.
            queue_messages.push_back(std::move(message));

            // Remove obsolete messages.
            const size_t queue_size_limit = get_queue_size_limit(header);
            while (!queue_messages.empty() && queue_messages.size() > queue_size_limit)
                queue_messages.pop_front();

        } while (client_->bytesAvailable() > 0);
    }


    unique_ptr<UiMessage> decode_plot_buffer_request()
    {
        assert(client_ != nullptr);

        auto response = new PlotBufferRequestMessage();
        MessageDecoder message_decoder(client_);
        message_decoder.read(response->buffer_name);

        Logger::instance()->info("Received request to provide symbol data: {}", response->buffer_name);

        return unique_ptr<UiMessage>(response);
    }

    unique_ptr<UiMessage> decode_get_observed_symbols_response()
    {
        assert(client_ != nullptr);

        auto response = new GetObservedSymbolsResponseMessage();

        MessageDecoder message_decoder(client_);
        message_decoder.read<std::deque<std::string>, std::string>(
            response->observed_symbols);


        {
            std::stringstream ss;
            bool isFirst = true;
            for (const auto& name : response->observed_symbols) {

                if (isFirst)
                    isFirst = false;
                else
                    ss << ", ";

                ss << name;
            }
            Logger::instance()->info("Received observed symbols: {}", ss.str());
        }

        return unique_ptr<UiMessage>(response);
    }

    std::unique_ptr<UiMessage> fetch_message(const MessageType& msg_type)
    {
        // Return message if it was already received before
        auto result = try_get_stored_message(msg_type);

        if (result != nullptr) {
            return result;
        }

        // Try to fetch message
        try_read_incoming_messages();

        return try_get_stored_message(msg_type);
    }


    void wait_for_client()
    {
        const static int timeoutConnectionMsec =
#if !defined(IS_DEVELOPMENT)
            10 * 1000;      // 10 seconds.
#else
            10 * 60 * 1000; // 10 minutes.
#endif
        
        if (client_ == nullptr) {

            if (!server_.waitForNewConnection(timeoutConnectionMsec))
                Logger::instance()->error("No clients connected to OpenImageDebugger server");

            client_ = server_.nextPendingConnection();

            Logger::instance()->info("UI app has been connected to OpenImageDebugger server");
        }
    }
};


AppHandler oid_initialize(int (*plot_callback)(const char*),
                          PyObject* optional_parameters)
{
    PyGILRAII py_gil_raii;

    Logger::set_logger_name("Bridge");

    if (optional_parameters != nullptr && !PyDict_Check(optional_parameters)) {

        const std::string error_str = "Invalid second parameter given to oid_initialize (was expecting a dict).";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_TypeError, error_str.c_str());
        return nullptr;
    }

    /*
     * Get optional fields
     */
    PyObject* py_oid_path =
        PyDict_GetItemString(optional_parameters, "oid_path");

    OidBridge* app = new OidBridge(plot_callback);

    if (py_oid_path) {
        string oid_path_str;
        copy_py_string(oid_path_str, py_oid_path);
        app->set_path(oid_path_str);
    }

    return static_cast<AppHandler>(app);
}


void oid_log_message(AppHandler handler, PyObject* level_py, PyObject* message_py)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_log_message received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    string level_str;
    copy_py_string(level_str, level_py);

    string message_str;
    copy_py_string(message_str, message_py);

    spdlog::level::level_enum level = spdlog::level::level_enum::info;
    if (level_str == "trace")
        level = spdlog::level::level_enum::trace;
    else if (level_str == "debug")
        level = spdlog::level::level_enum::debug;
    else if (level_str == "info")
        level = spdlog::level::level_enum::info;
    else if (level_str == "warning")
        level = spdlog::level::level_enum::warn;
    else if (level_str == "error")
        level = spdlog::level::level_enum::err;
    else if (level_str == "critical")
        level = spdlog::level::level_enum::critical;

    app->log_message(level, message_str);
}


void oid_cleanup(AppHandler handler)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_cleanup received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    delete app;
}


void oid_exec(AppHandler handler)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_exec received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    app->start();
}


int oid_is_window_ready(AppHandler handler)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_is_window_ready received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return 0;
    }

    return app->is_window_ready();
}


PyObject* oid_get_observed_buffers(AppHandler handler)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_get_observed_buffers received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return nullptr;
    }

    auto observed_symbols = app->get_observed_symbols();
    PyObject* py_observed_symbols =
        PyList_New(static_cast<Py_ssize_t>(observed_symbols.size()));

    int observed_symbols_sentinel = static_cast<int>(observed_symbols.size());
    for (int i = 0; i < observed_symbols_sentinel; ++i) {
        string symbol_name       = observed_symbols[i];
        PyObject* py_symbol_name = PyBytes_FromString(symbol_name.c_str());

        if (py_symbol_name == nullptr) {
            Py_DECREF(py_observed_symbols);
            return nullptr;
        }

        PyList_SetItem(py_observed_symbols, i, py_symbol_name);
    }

    return py_observed_symbols;
}


void oid_set_available_symbols(AppHandler handler, PyObject* available_vars_py)
{
    PyGILRAII py_gil_raii;

    assert(PyList_Check(available_vars_py));

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_set_available_symbols received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    deque<string> available_vars_stl;
    for (Py_ssize_t pos = 0; pos < PyList_Size(available_vars_py); ++pos) {
        string var_name_str;
        PyObject* listItem = PyList_GetItem(available_vars_py, pos);
        copy_py_string(var_name_str, listItem);
        available_vars_stl.push_back(var_name_str);
    }

    app->set_available_symbols(available_vars_stl);
}


void oid_run_event_loop(AppHandler handler)
{
    PyGILRAII py_gil_raii;

    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_run_event_loop received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    app->run_event_loop();
}


void oid_plot_buffer(AppHandler handler, PyObject* buffer_metadata)
{
    PyGILRAII py_gil_raii;


    OidBridge* app = static_cast<OidBridge*>(handler);

    if (app == nullptr) {

        const std::string error_str = "oid_plot_buffer received null application handler";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_RuntimeError, error_str.c_str());
        return;
    }

    if (!PyDict_Check(buffer_metadata)) {

        const std::string error_str = "Invalid object given to oid_plot_buffer (was expecting a dict)";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_TypeError, error_str.c_str());
        return;
    }

    /*
     * Get required fields
     */
    PyObject* py_variable_name =
        PyDict_GetItemString(buffer_metadata, "variable_name");
    PyObject* py_display_name =
        PyDict_GetItemString(buffer_metadata, "display_name");
    PyObject* py_pointer  = PyDict_GetItemString(buffer_metadata, "pointer");
    PyObject* py_width    = PyDict_GetItemString(buffer_metadata, "width");
    PyObject* py_height   = PyDict_GetItemString(buffer_metadata, "height");
    PyObject* py_channels = PyDict_GetItemString(buffer_metadata, "channels");
    PyObject* py_type     = PyDict_GetItemString(buffer_metadata, "type");
    PyObject* py_row_stride =
        PyDict_GetItemString(buffer_metadata, "row_stride");
    PyObject* py_pixel_layout =
        PyDict_GetItemString(buffer_metadata, "pixel_layout");

    /*
     * Get optional fields
     */
    PyObject* py_transpose_buffer =
        PyDict_GetItemString(buffer_metadata, "transpose_buffer");
    bool transpose_buffer = false;
    if (py_transpose_buffer != nullptr) {
        CHECK_FIELD_TYPE(transpose_buffer, PyBool_Check, "transpose_buffer");
        transpose_buffer = PyObject_IsTrue(py_transpose_buffer);
    }

    /*
     * Check if expected fields were provided
     */
    CHECK_FIELD_PROVIDED(variable_name, "plot_buffer");
    CHECK_FIELD_PROVIDED(display_name, "plot_buffer");
    CHECK_FIELD_PROVIDED(pointer, "plot_buffer");
    CHECK_FIELD_PROVIDED(width, "plot_buffer");
    CHECK_FIELD_PROVIDED(height, "plot_buffer");
    CHECK_FIELD_PROVIDED(channels, "plot_buffer");
    CHECK_FIELD_PROVIDED(type, "plot_buffer");
    CHECK_FIELD_PROVIDED(row_stride, "plot_buffer");
    CHECK_FIELD_PROVIDED(pixel_layout, "plot_buffer");

    /*
     * Check if expected fields have the correct types
     */
    CHECK_FIELD_TYPE(variable_name, check_py_string_type, "plot_buffer");
    CHECK_FIELD_TYPE(display_name, check_py_string_type, "plot_buffer");
    CHECK_FIELD_TYPE(width, PY_INT_CHECK_FUNC, "plot_buffer");
    CHECK_FIELD_TYPE(height, PY_INT_CHECK_FUNC, "plot_buffer");
    CHECK_FIELD_TYPE(channels, PY_INT_CHECK_FUNC, "plot_buffer");
    CHECK_FIELD_TYPE(type, PY_INT_CHECK_FUNC, "plot_buffer");
    CHECK_FIELD_TYPE(row_stride, PY_INT_CHECK_FUNC, "plot_buffer");
    CHECK_FIELD_TYPE(pixel_layout, check_py_string_type, "plot_buffer");

#if PY_MAJOR_VERSION == 2
    auto pybuffer_deleter = [](Py_buffer* buff) {
        PyBuffer_Release(buff);
        delete buff;
    };
    std::unique_ptr<Py_buffer, decltype(pybuffer_deleter)> py_buff(
        nullptr, pybuffer_deleter);
#endif

    // Retrieve pointer to buffer
    uint8_t* buff_ptr = nullptr;
    size_t buff_size = 0;
    if (PyMemoryView_Check(py_pointer) != 0) {
        
        get_c_ptr_from_py_buffer(py_pointer, buff_ptr, buff_size);
    }
#if PY_MAJOR_VERSION == 2
    else if (PyBuffer_Check(py_pointer) != 0) {
        py_buff.reset(new Py_buffer());
        PyObject_GetBuffer(py_pointer, py_buff.get(), PyBUF_SIMPLE);
        buff_ptr = reinterpret_cast<uint8_t*>(py_buff->buf);
        buff_size = static_cast<size_t>(py_buff->len);
    }
#endif
    else {

        const std::string error_str = "Could not retrieve C pointer to provided buffer";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_TypeError, error_str.c_str());
        return;
    }

    /*
     * Send buffer contents
     */
    string variable_name_str;
    string display_name_str;
    string pixel_layout_str;

    copy_py_string(variable_name_str, py_variable_name);
    copy_py_string(display_name_str, py_display_name);
    copy_py_string(pixel_layout_str, py_pixel_layout);

    auto buff_width    = static_cast<int>(get_py_int(py_width));
    auto buff_height   = static_cast<int>(get_py_int(py_height));
    auto buff_channels = static_cast<int>(get_py_int(py_channels));
    auto buff_stride   = static_cast<int>(get_py_int(py_row_stride));

    BufferType buff_type = static_cast<BufferType>(get_py_int(py_type));

    const size_t buff_size_expected =
        static_cast<size_t>(buff_stride * buff_height * buff_channels) *
        typesize(buff_type);

    if (buff_ptr == nullptr) {

        const std::string error_str = "oid_plot_buffer received nullptr as buffer pointer";
        Logger::instance()->error(error_str);
        RAISE_PY_EXCEPTION(PyExc_TypeError, error_str.c_str());
        return;
    }

    if (buff_size < buff_size_expected) {

        const std::string error_str = "oid_plot_buffer received shorter buffer then expected";
        Logger::instance()->error(error_str +
                            ". Variable name {}"
                            ". Expected {} bytes"
                            ". Received {} bytes",
                            variable_name_str, buff_size_expected, buff_size);
        RAISE_PY_EXCEPTION(PyExc_TypeError, error_str.c_str());
        return;
    }

    app->plot_buffer(variable_name_str,
                     display_name_str,
                     pixel_layout_str,
                     transpose_buffer,
                     buff_width,
                     buff_height,
                     buff_channels,
                     buff_stride,
                     buff_type,
                     buff_ptr,
                     buff_size);
}
