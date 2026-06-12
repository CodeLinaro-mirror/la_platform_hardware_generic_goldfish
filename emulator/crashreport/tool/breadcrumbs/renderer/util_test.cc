// Copyright 2026 The Android Open Source Project
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
// limitations under complying with the License.
#include "android/crashreport/breadcrumbs/util.h"

#include <gtest/gtest.h>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace android::crashreport::breadcrumbs {

TEST(UtilTest, FormatEventTime) {
    uint64_t start_ts = 3600ULL * 1000000000ULL;
    uint64_t event_ts = start_ts + 1234567890ULL;

    std::string result = FormatEventTime(event_ts, start_ts);
    std::string expected =
            absl::StrFormat("01:00:01.234567 (+%v)", absl::Nanoseconds(1234567890ULL));
    EXPECT_EQ(result, expected);
}

TEST(UtilTest, FormatEventTimeZeroOffset) {
    uint64_t start_ts = 3600ULL * 1000000000ULL;
    std::string result = FormatEventTime(start_ts, start_ts);
    std::string expected = absl::StrFormat("01:00:00.000000 (+%v)", absl::Nanoseconds(0));
    EXPECT_EQ(result, expected);
}

}  // namespace android::crashreport::breadcrumbs
