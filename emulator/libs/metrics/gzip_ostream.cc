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

#include <zlib.h>

#include <algorithm>
#include <streambuf>
#include <vector>

#include "gzip_ostream.h"

namespace goldfish::metrics {

namespace {

class GzipOutputStreambuf : public std::streambuf {
  public:
    GzipOutputStreambuf(std::streambuf* dst, int level = Z_DEFAULT_COMPRESSION,
                        std::size_t bufferCapacity = k16KB)
            : mDst(dst), mCapacity(bufferCapacity), mIn(mCapacity), mOut(mCapacity) {
        const int GZIP_WINDOW_BITS = 15 + 16;
        mErr = deflateInit2(&mZstream, level, Z_DEFLATED, GZIP_WINDOW_BITS, 8, Z_DEFAULT_STRATEGY);

        setp(mIn.data(), mIn.data() + mIn.size());
    }

    ~GzipOutputStreambuf() override {
        sync();
        deflateEnd(&mZstream);
    }

  private:
    bool compress(int flush) {
        int written = 0;

        do {
            mZstream.next_out = reinterpret_cast<Bytef*>(mOut.data());
            mZstream.avail_out = mOut.size();
            mErr = deflate(&mZstream, flush);
            if (mErr != Z_OK && mErr != Z_STREAM_END && mErr != Z_BUF_ERROR) return false;

            int cnt = reinterpret_cast<char*>(mZstream.next_out) - mOut.data();
            written = mDst->sputn(mOut.data(), cnt);
            if (written != cnt) {
                return false;
            }
        } while (mErr != Z_STREAM_END && mErr != Z_BUF_ERROR && written != 0);

        return true;
    }

    std::streambuf::int_type overflow(std::streambuf::int_type c = traits_type::eof()) override {
        mZstream.next_in = reinterpret_cast<Bytef*>(pbase());
        mZstream.avail_in = pptr() - pbase();
        while (mZstream.avail_in > 0) {
            if (!compress(Z_NO_FLUSH)) {
                setp(nullptr, nullptr);
                return traits_type::eof();
            }
        }
        setp(mIn.data(), mIn.data() + mIn.size());
        return c == traits_type::eof() ? traits_type::eof() : sputc(c);
    }

    int sync() override {
        overflow();
        if (!pptr()) return -1;

        mZstream.next_in = nullptr;
        mZstream.avail_in = 0;
        if (!compress(Z_FINISH)) return -1;

        deflateReset(&mZstream);
        return mDst->pubsync();
    }

    static constexpr std::size_t k16KB = 16 * 1024;
    std::streambuf* mDst;
    std::size_t mCapacity;
    std::vector<char> mIn;
    std::vector<char> mOut;
    z_stream mZstream{};
    int mErr{Z_OK};
};

}  // namespace

GzipOutputStream::GzipOutputStream(std::ostream& os)
        : std::ostream(new GzipOutputStreambuf(os.rdbuf())) {}

GzipOutputStream::~GzipOutputStream() {
    delete rdbuf();
}

}  // namespace goldfish::metrics
