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

#include "message_decoder.h"


MessageDecoder::MessageDecoder(QTcpSocket* socket)
    : socket_(socket)
{
}

void MessageDecoder::read_impl(char* dst, size_t read_length)
{
    size_t offset = 0;
    do {
        offset += socket_->read(dst + offset,
                                static_cast<qint64>(read_length - offset));

        if (offset < read_length) {
            socket_->waitForReadyRead();
        }
    } while (offset < read_length);
}

template <>
MessageDecoder& MessageDecoder::read<std::vector<uint8_t>>(std::vector<uint8_t>& container)
{
    size_t container_size;
    read(container_size);

    container.resize(container_size);
    read_impl(reinterpret_cast<char*>(container.data()), container_size);

    return *this;
}

template <>
MessageDecoder& MessageDecoder::read<std::string>(std::string& value)
{
    size_t symbol_length;
    read(symbol_length);

    value.resize(symbol_length);
    read_impl(&value.front(), static_cast<qint64>(symbol_length));

    return *this;
}

template <>
MessageDecoder& MessageDecoder::read<QString>(QString& value)
{
    size_t symbol_length;
    read(symbol_length);

    std::vector<char> temp_string;
    temp_string.resize(symbol_length + 1, '\0');
    read_impl(reinterpret_cast<char*>(temp_string.data()), symbol_length);
    value = QString(temp_string.data());

    return *this;
}
