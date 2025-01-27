// Copyright 2022 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#pragma once
#include <cstring>
#include <streambuf>

#include "client/annotation.h"

namespace android {
namespace crashreport {

using crashpad::Annotation;

/**
 * @brief A streambuffer that writes to a crashpad annotation.
 *
 * Use your favorite std::stream to write data that ends up in a minidump.
 *
 * @note `AnnotationStreambuf` should be declared with static storage duration!
 *
 * @tparam MaxSize The maximum size of the annotation buffer.
 *
 * Example usage:
 * @code
 * static android::crashreport::AnnotationStreambuf<4096> mLogBuf{"my_label"};
 * std::ostream log(&mLogBuf);
 * log << "Some useful info when you crash later on.";
 * @endcode
 */
template <Annotation::ValueSizeType MaxSize>
class AnnotationStreambuf : public crashpad::Annotation, public std::streambuf {
  public:
    AnnotationStreambuf(const AnnotationStreambuf&) = delete;
    AnnotationStreambuf& operator=(const AnnotationStreambuf&) = delete;

    /**
     * @brief Constructor.
     *
     * @param name The name of the annotation. This is how it will show up
     * in a minidump.
     */
    explicit AnnotationStreambuf(const char name[])
        : Annotation(Type::kString, name, mBuffer), mBuffer() {
        setp(mBuffer, mBuffer + MaxSize);
    }

    /**
     * @brief Synchronizes the buffer. Mainly for testing.
     * @return 0 on success.
     */
    int sync() override {
        setg(mBuffer, mBuffer, mBuffer + (pptr() - pbase()));
        return 0;
    }

  protected:
    /**
     * @brief Writes a sequence of characters to the buffer.
     * @param s Pointer to the character sequence.
     * @param n Number of characters to write.
     * @return The number of characters written.
     */
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::streamsize available = epptr() - pptr();
        std::streamsize to_copy = std::min(n, available);
        if (available <= 0) {
            return traits_type::eof();  // No space left in the buffer
        }
        std::memcpy(pptr(), s, static_cast<std::size_t>(to_copy));
        pbump(static_cast<int>(to_copy));

        SetSize(pptr() - pbase());
        return to_copy;
    };

    /**
     * @brief Handles overflow.
     * @param ch The character causing the overflow.
     * @return traits_type::eof().
     */
    int_type overflow(int_type ch) override { return traits_type::eof(); }

    /**
     * @brief Handles underflow.
     * @return The next character in the buffer or traits_type::eof().
     */
    int_type underflow() override { return gptr() == egptr() ? traits_type::eof() : *gptr(); }

  private:
    char mBuffer[MaxSize];
};

/**
 * @brief A stream buffer that can hold 8kb of annotation data.
 */
using DefaultAnnotationStreambuf = AnnotationStreambuf<8192>;
}  // namespace crashreport
}  // namespace android
