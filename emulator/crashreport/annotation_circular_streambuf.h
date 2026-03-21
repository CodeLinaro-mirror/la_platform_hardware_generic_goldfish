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
#include <cassert>
#include <cstring>
#include <mutex>
#include <streambuf>
#include <utility>

#include "client/annotation.h"

namespace android::crashreport {

// #define DEBUG_CRASH
#ifdef DEBUG_CRASH
#define DD_AN(fmt, ...)                                                                  \
    fprintf(stderr, "AnnotationCircularStreambuf: %s:%d| " fmt "\n", __func__, __LINE__, \
            ##__VA_ARGS__)
#else
#define DD_AN(...) (void)0
#endif

using crashpad::Annotation;

/**
 * @brief A circular streambuffer that writes to a crashpad annotation.
 *
 * Use your favorite `std::stream` to write data that ends up in a minidump.
 * This is a circular buffer, meaning it overwrites old data when full.
 *
 * @note `AnnotationCircularStreambuf` should be declared with static storage duration!
 *
 * @tparam MaxSize The maximum size of the annotation buffer.
 *
 * Example usage:
 * @code
 * static android::crashreport::AnnotationCircularStreambuf<4096> mLogBuf{"my_label"};
 * std::ostream log(&mLogBuf);
 * log << "Some useful info when you crash later on."; // Older data will be overwritten as needed.
 * @endcode
 */
template <Annotation::ValueSizeType MaxSize>
class AnnotationCircularStreambuf : public std::streambuf {
  public:
    AnnotationCircularStreambuf(const AnnotationCircularStreambuf&) = delete;
    AnnotationCircularStreambuf& operator=(const AnnotationCircularStreambuf&) = delete;

    /**
     * @brief Constructor.
     *
     * @param name The name of the annotation. This is how it will show up in a minidump.
     */
    explicit AnnotationCircularStreambuf(std::string name)
            : name_(std::move(name))
            , buffer_()
            , annotation_(Annotation::Type::kString, name_.c_str(), buffer_) {
        setp(buffer_, buffer_ + MaxSize);
    }

    /**
     * @brief Synchronizes the buffer. Primarily for testing.
     * @return 0 on success.
     */
    int sync() override {
        setg(buffer_, buffer_, buffer_ + size_);
        return 0;
    }

  protected:
    /**
     * @brief Writes a sequence of characters to the circular buffer.
     * @param s Pointer to the character sequence.
     * @param n Number of characters to write.
     * @return The number of characters written.  If the input size `n` exceeds
     * `MaxSize`, the returned value will still be `n`, even though only the
     * last `MaxSize` characters are effectively retained due to the circular
     * nature of the buffer.
     */
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        const std::lock_guard<std::mutex> lock(buffer_access_);
        std::streamsize to_write = n;
        std::streamsize written = 0;
        const char* p = s;
        DD_AN("Writing %.*s\n", n, s);

        // Optimization: If writing more than the buffer size, skip initial
        // characters that would be immediately overwritten.
        if (std::cmp_greater(to_write, MaxSize)) {
            p += (to_write - MaxSize);
            written = to_write - MaxSize;
            to_write = MaxSize;
        }

        while (to_write > 0) {
            if (pptr() == epptr()) {  // Wrap around if buffer is full
                setp(buffer_, buffer_ + MaxSize);
                size_ = MaxSize;
                annotation_.SetSize(size_);
            }
            const std::streamsize available = epptr() - pptr();
            const std::streamsize to_copy = std::min(to_write, available);
            assert(available > 0);
            assert(to_copy > 0);

            std::memcpy(pptr(), p, static_cast<std::size_t>(to_copy));
            pbump(static_cast<int>(to_copy));

            p += to_copy;
            written += to_copy;
            to_write -= to_copy;
        }

        if (size_ != MaxSize) {
            size_ = pptr() - pbase();
            annotation_.SetSize(size_);
        }
        return written;
    };

    /**
     * @brief Handles overflow. Returns eof.
     * @param ch The overflowing character
     * @return traits_type::eof().
     */
    int_type overflow(int_type /*ch*/) override { return traits_type::eof(); }

    /**
     * @brief Handles underflow.
     * @return The next character or traits_type::eof()
     */
    int_type underflow() override { return gptr() == egptr() ? traits_type::eof() : *gptr(); }

  private:
    std::mutex buffer_access_;
    std::string name_;
    char buffer_[MaxSize];
    int size_ = 0;
    Annotation annotation_;
};

/**
 * @brief A circular stream buffer with an 8kb capacity for annotation data.
 */
using DefaultAnnotationCircularStreambuf = AnnotationCircularStreambuf<8192>;

}  // namespace android::crashreport
