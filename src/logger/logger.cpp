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

#include <ctime>
#include <chrono>
#include <spdlog/sinks/basic_file_sink.h>

#include "logger.h"


const std::shared_ptr<spdlog::logger>& Logger::instance()
{
    static Logger logger;

    return logger.logger();
}

Logger::Logger() :
    logger_(init_logger())
{

}

const std::shared_ptr<spdlog::logger>& Logger::logger() const
{
    return logger_;
}

std::string Logger::get_current_time_str()
{
    const static std::string format = "%y%m%d %H%M%S";

    const auto clock_now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(clock_now);

    char mbstr[100];
    const tm* pointerTm = std::gmtime(&time_now);	// Convert into a GMT timezone instead of a local one.
    if (pointerTm == nullptr)
        return std::string();

    const bool is_succeed = std::strftime(mbstr, sizeof(mbstr), format.c_str(), pointerTm);
    if (!is_succeed)
        return std::string();

    mbstr[sizeof(mbstr) - 1] = '\0';
    return std::string(mbstr);
}

std::shared_ptr<spdlog::logger> Logger::init_logger()
{
    try {

        const std::string path_dir_str = "/usr/local/bin/OpenImageDebugger/logs/";
        const std::string time_str = get_current_time_str();


        // Initialize file logger.
        return spdlog::basic_logger_mt("OID", path_dir_str + time_str + ".txt");

        spdlog::flush_every(std::chrono::seconds(5));
        spdlog::flush_on(spdlog::level::warn);
    } catch (const spdlog::spdlog_ex& ex) {

        // Use default cout logger.
        auto logger = spdlog::default_logger();
        logger->warn("File logger init failed: {}", ex.what());
        return logger;
    }
}
