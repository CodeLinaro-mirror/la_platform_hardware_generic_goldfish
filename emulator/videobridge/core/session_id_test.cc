// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/videobridge/session_id.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <unordered_set>

namespace goldfish::videobridge {
namespace {

TEST(SessionIdTest, GeneratesExpectedUuidFromRawBits) {
    // High: 0x0123456789abcdef -> time_low: 01234567, time_mid: 89ab, time_hi_and_version: 4def
    // Low:  0x0123456789abcdef -> clock_seq: 8123, node: 456789abcdef
    EXPECT_EQ(GenerateSessionId(0x0123456789abcdefULL, 0x0123456789abcdefULL),
              "01234567-89ab-4def-8123-456789abcdef");
}

TEST(SessionIdTest, HandlesAllZeros) {
    EXPECT_EQ(GenerateSessionId(0, 0), "00000000-0000-4000-8000-000000000000");
}

TEST(SessionIdTest, HandlesAllOnes) {
    EXPECT_EQ(GenerateSessionId(UINT64_MAX, UINT64_MAX), "ffffffff-ffff-4fff-bfff-ffffffffffff");
}

// Regression test for the zero padded width. An earlier copy of this helper used "%12x" rather
// than "%012x", which space pads the node field and yields ids containing spaces.
TEST(SessionIdTest, NodeFieldIsZeroPaddedNotSpacePadded) {
    EXPECT_EQ(GenerateSessionId(0, 1), "00000000-0000-4000-8000-000000000001");
}

TEST(SessionIdTest, RandomGeneratorHasCanonicalLayoutAndVariant) {
    const std::string id = GenerateSessionId();

    ASSERT_EQ(id.size(), 36);
    EXPECT_EQ(id[8], '-');
    EXPECT_EQ(id[13], '-');
    EXPECT_EQ(id[14], '4');
    EXPECT_EQ(id[18], '-');
    EXPECT_TRUE(id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');
    EXPECT_EQ(id[23], '-');
}

TEST(SessionIdTest, GeneratesDistinctValues) {
    std::unordered_set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(seen.insert(GenerateSessionId()).second) << "Duplicate session id generated.";
    }
}

}  // namespace
}  // namespace goldfish::videobridge
