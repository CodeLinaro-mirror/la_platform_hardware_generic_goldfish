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
// limitations under the License.

#include "device.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace android::goldfish {
namespace {

// Reads one option value from `input` the same way as QEMU's get_opt_value()
// in util/qemu-option.c: the value ends at the first single comma, and a doubled
// comma is read as a literal comma. Stores the unparsed remainder of `input`,
// starting at the terminating comma (if any), in `rest`.
std::string ParseQemuOptionValue(std::string_view input, std::string_view* rest) {
    std::string value;
    size_t pos = 0;
    while (pos < input.size()) {
        if (input[pos] == ',') {
            if (pos + 1 == input.size() || input[pos + 1] != ',') {
                break;
            }
            ++pos;  // Skip the escaping comma.
        }
        value.push_back(input[pos]);
        ++pos;
    }
    *rest = input.substr(pos);
    return value;
}

// The AVD display name reported in b/573235492.
constexpr std::string_view kBugDisplayName = "°º¤ø,¸¸,ø¤º°`°º¤ø, UTF-8 ¸,ø¤°º¤ø,¸¸,ø¤º°`°º¤ø,¸";

TEST(DeviceTest, EscapeQemuOptionValueLeavesValuesWithoutCommasUnchanged) {
    EXPECT_EQ(Device::EscapeQemuOptionValue(""), "");
    EXPECT_EQ(Device::EscapeQemuOptionValue("Pixel_8_API_37"), "Pixel_8_API_37");
    EXPECT_EQ(Device::EscapeQemuOptionValue("17 (C) - API 37"), "17 (C) - API 37");
    EXPECT_EQ(Device::EscapeQemuOptionValue("a=b"), "a=b");
    EXPECT_EQ(Device::EscapeQemuOptionValue("°º¤ø UTF-8 ¸"), "°º¤ø UTF-8 ¸");
}

TEST(DeviceTest, EscapeQemuOptionValueDoublesEveryComma) {
    EXPECT_EQ(Device::EscapeQemuOptionValue(","), ",,");
    EXPECT_EQ(Device::EscapeQemuOptionValue("a,b"), "a,,b");
    EXPECT_EQ(Device::EscapeQemuOptionValue(",a,"), ",,a,,");
    EXPECT_EQ(Device::EscapeQemuOptionValue("a,,b"), "a,,,,b");
    EXPECT_EQ(Device::EscapeQemuOptionValue("/home/Doe, Jane/.android/avd/test.avd"),
              "/home/Doe,, Jane/.android/avd/test.avd");
    EXPECT_EQ(Device::EscapeQemuOptionValue(kBugDisplayName),
              "°º¤ø,,¸¸,,ø¤º°`°º¤ø,, UTF-8 ¸,,ø¤°º¤ø,,¸¸,,ø¤º°`°º¤ø,,¸");
}

TEST(DeviceTest, QemuParsesEscapedValueBackToOriginal) {
    constexpr std::string_view kValues[] = {
        "", ",", ",,", "a,", ",a", "a,b,,c", "/home/Doe, Jane/test.avd", kBugDisplayName,
    };
    for (std::string_view value : kValues) {
        SCOPED_TRACE(value);
        // The escaped value is followed by the next option, as in
        // "-device avdstart,avd_name=<value>,avd_id=...".
        const std::string option = Device::EscapeQemuOptionValue(value) + ",next=1";
        std::string_view rest;
        EXPECT_EQ(ParseQemuOptionValue(option, &rest), value);
        EXPECT_EQ(rest, ",next=1");
    }
}

}  // namespace
}  // namespace android::goldfish
