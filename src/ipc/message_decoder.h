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

#pragma once

#include <vector>
#include <string>

#include <QTcpSocket>

#include "message_type.h"
#include "primitive_block.h"
#include "raw_data_decode.h"


class MessageDecoder
{
  public:
    MessageDecoder(QTcpSocket* socket);

    template <typename PrimitiveType>
    MessageDecoder& read(PrimitiveType& value)
    {
        assert_primitive_type<PrimitiveType>();

        read_impl(reinterpret_cast<char*>(&value), sizeof(PrimitiveType));

        return *this;
    }

    template <typename StringContainer, typename StringType>
    MessageDecoder& read(StringContainer& symbol_container)
    {
        size_t number_symbols;
        read(number_symbols);

        for (int s = 0; s < static_cast<int>(number_symbols); ++s) {
            StringType symbol_value;
            read(symbol_value);

            symbol_container.push_back(symbol_value);
        }

        return *this;
    }

  private:
    QTcpSocket* socket_;

    void read_impl(char* dst, size_t read_length);
};

template <>
MessageDecoder& MessageDecoder::read<std::vector<uint8_t>>(std::vector<uint8_t>& container);

template <>
MessageDecoder& MessageDecoder::read<std::string>(std::string& value);

template <>
MessageDecoder& MessageDecoder::read<QString>(QString& value);
