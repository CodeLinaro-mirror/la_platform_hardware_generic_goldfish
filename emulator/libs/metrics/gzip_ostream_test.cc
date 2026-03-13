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

#include <gtest/gtest.h>
#include <zlib.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace goldfish::metrics {

namespace {

constexpr std::string_view kRickRoll = R"(Never gonna give you up
Never gonna let you down
Never gonna run around
And desert you
Never gonna make you cry
Never gonna say goodbye
Never gonna tell a lie
And hurt you)";

std::string Decompress(const std::string& compressed) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    // 15 + 16 for gzip header/footer
    if (inflateInit2(&zs, 15 + 16) != Z_OK) {
        return "inflateInit2 failed";
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    zs.avail_in = compressed.size();

    int ret;
    char outbuffer[32768];
    std::string outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = inflate(&zs, 0);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);

    if (ret != Z_STREAM_END) {
        return "Decompression failed with error " + std::to_string(ret);
    }

    return outstring;
}

}  // namespace

TEST(GzipOutputStreamTest, CompressSimpleString) {
    std::stringstream ss;
    {
        GzipOutputStream gzip(ss);
        gzip << kRickRoll;
    }

    std::string compressed = ss.str();
    EXPECT_FALSE(compressed.empty());

    std::string decompressed = Decompress(compressed);
    EXPECT_EQ(decompressed, kRickRoll);
}

TEST(GzipOutputStreamTest, CompressLargeData) {
    std::string largeData;
    for (int i = 0; i < 10000; ++i) {
        largeData += "Iteration " + std::to_string(i) + "\n";
    }

    std::stringstream ss;
    {
        GzipOutputStream gzip(ss);
        gzip << largeData;
    }

    std::string compressed = ss.str();
    EXPECT_LT(compressed.size(), largeData.size());

    std::string decompressed = Decompress(compressed);
    EXPECT_EQ(decompressed, largeData);
}

}  // namespace goldfish::metrics
