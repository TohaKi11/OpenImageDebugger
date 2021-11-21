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

#include <deque>
#include <string>

#include <QTcpSocket>

#include "message_type.h"
#include "primitive_block.h"


class MessageComposer
{
  public:
    MessageComposer(QTcpSocket* socket);

    template <typename PrimitiveType>
    MessageComposer& push(const PrimitiveType& value)
    {
        assert_primitive_type<PrimitiveType>();

        message_blocks_.emplace_back(new PrimitiveBlock<PrimitiveType>(value));

        return *this;
    }

    MessageComposer& push(uint8_t* buffer, size_t size);

    void send() const;

    void clear();

  private:
    QTcpSocket* socket_;
    std::deque<std::unique_ptr<MessageBlock>> message_blocks_;
};

template <>
MessageComposer& MessageComposer::push<std::string>(const std::string& value);

template <>
MessageComposer& MessageComposer::push<std::deque<std::string>>(const std::deque<std::string>& container);

