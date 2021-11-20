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

#include "message_composer.h"
#include "buffer_block.h"
#include "string_block.h"


MessageComposer& MessageComposer::push(uint8_t* buffer, size_t size)
{
    push(size);
    message_blocks_.emplace_back(new BufferBlock(buffer, size));

    return *this;
}

void MessageComposer::send(QTcpSocket* socket) const
{
    for (const auto& block : message_blocks_) {
        qint64 offset = 0;
        do {
            offset +=
                socket->write(reinterpret_cast<const char*>(block->data()),
                              static_cast<qint64>(block->size()));

            if (offset < static_cast<qint64>(block->size())) {
                socket->waitForBytesWritten();
            }
        } while (offset < static_cast<qint64>(block->size()));
    }

    socket->waitForBytesWritten();
}

void MessageComposer::clear()
{
    message_blocks_.clear();
}

template <>
MessageComposer& MessageComposer::push<std::string>(const std::string& value)
{
    push(value.size());
    message_blocks_.emplace_back(new StringBlock(value));
    return *this;
}

template <>
MessageComposer& MessageComposer::push<std::deque<std::string>>(const std::deque<std::string>& container)
{
    push(container.size());
    for (const auto& value : container) {
        push(value);
    }
    return *this;
}
