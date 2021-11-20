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

#include <type_traits>

#include "message_block.h"
#include "message_type.h"
#include "raw_data_decode.h"


template <typename Primitive>
struct PrimitiveBlock : public MessageBlock
{
    PrimitiveBlock(Primitive value)
        : data_(value)
    {
    }

    ~PrimitiveBlock()
    {
    }

    virtual size_t size() const
    {
        return sizeof(Primitive);
    }

    virtual const uint8_t* data() const
    {
        return reinterpret_cast<const uint8_t*>(&data_);
    }

  private:
    Primitive data_;
};

template <typename PrimitiveType>
void assert_primitive_type()
{
    static_assert(std::is_same<PrimitiveType, MessageType>::value ||
                      std::is_same<PrimitiveType, int>::value ||
                      std::is_same<PrimitiveType, unsigned char>::value ||
                      std::is_same<PrimitiveType, BufferType>::value ||
                      std::is_same<PrimitiveType, bool>::value ||
                      std::is_same<PrimitiveType, std::size_t>::value,
                  "this function must only be called with primitives");
}
