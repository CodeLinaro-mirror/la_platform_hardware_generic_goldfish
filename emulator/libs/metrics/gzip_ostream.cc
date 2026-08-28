// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gzip_ostream.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <streambuf>
#include <vector>

namespace goldfish::metrics {

namespace {

class GzipOutputStreambuf : public std::streambuf {
  public:
    explicit GzipOutputStreambuf(std::streambuf* dst, int level = Z_DEFAULT_COMPRESSION,
                                 std::size_t buffer_capacity = kSixteenKilobytes)
            : dst_(dst), capacity_(buffer_capacity), in_(capacity_), out_(capacity_) {
        const int gzip_window_bits = 15 + 16;
        err_ = deflateInit2(&zstream_, level, Z_DEFLATED, gzip_window_bits, 8, Z_DEFAULT_STRATEGY);

        setp(in_.data(), in_.data() + in_.size());
    }

    ~GzipOutputStreambuf() override {
        sync();
        deflateEnd(&zstream_);
    }

  private:
    bool Compress(int flush) {
        int written = 0;

        do {
            zstream_.next_out = reinterpret_cast<Bytef*>(out_.data());
            zstream_.avail_out = out_.size();
            err_ = deflate(&zstream_, flush);
            if (err_ != Z_OK && err_ != Z_STREAM_END && err_ != Z_BUF_ERROR) return false;

            int cnt = reinterpret_cast<char*>(zstream_.next_out) - out_.data();
            written = dst_->sputn(out_.data(), cnt);
            if (written != cnt) {
                return false;
            }
        } while (err_ != Z_STREAM_END && err_ != Z_BUF_ERROR && written != 0);

        return true;
    }

    std::streambuf::int_type overflow(std::streambuf::int_type c = traits_type::eof()) override {
        zstream_.next_in = reinterpret_cast<Bytef*>(pbase());
        zstream_.avail_in = pptr() - pbase();
        while (zstream_.avail_in > 0) {
            if (!Compress(Z_NO_FLUSH)) {
                setp(nullptr, nullptr);
                return traits_type::eof();
            }
        }
        setp(in_.data(), in_.data() + in_.size());
        return c == traits_type::eof() ? traits_type::eof() : sputc(c);
    }

    int sync() override {
        overflow();
        if (!pptr()) return -1;

        zstream_.next_in = nullptr;
        zstream_.avail_in = 0;
        if (!Compress(Z_FINISH)) return -1;

        deflateReset(&zstream_);
        return dst_->pubsync();
    }

    static constexpr std::size_t kSixteenKilobytes = 16ULL * 1024;
    std::streambuf* dst_;
    std::size_t capacity_;
    std::vector<char> in_;
    std::vector<char> out_;
    z_stream zstream_{};
    int err_{Z_OK};
};

}  // namespace

GzipOutputStream::GzipOutputStream(std::ostream& os)
        : std::ostream(new GzipOutputStreambuf(os.rdbuf())) {}

GzipOutputStream::~GzipOutputStream() {
    delete rdbuf();
}

}  // namespace goldfish::metrics
