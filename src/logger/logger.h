/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2019 OpenImageDebugger
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

#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <spdlog/spdlog.h>


class Logger
{
public:
    static void set_file_name(const std::string& file_name);
    static void set_logger_name(const std::string& logger_name);

    static const std::string& get_file_name();
    static const std::string& get_logger_name();

    static const std::shared_ptr<spdlog::logger>& instance();

private:
    Logger();

    Logger(Logger const&) = delete;
    Logger& operator=(Logger const&) = delete;

    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    static Logger& instance_wrapper();

private:
    const std::shared_ptr<spdlog::logger>& logger();

private:
    static std::string get_current_time_str();
    void init_logger();

private:
    std::string file_name_;
    std::string logger_name_;

    mutable std::mutex mutex_;
    std::shared_ptr<spdlog::logger> logger_;
};
