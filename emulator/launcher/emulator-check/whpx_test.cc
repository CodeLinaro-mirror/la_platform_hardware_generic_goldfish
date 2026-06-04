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

#include "whpx.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <windows.h>

TEST(WhpxTest, checkWHPX) {
    auto res = android::checkWHPX();
    if (res.first != 0) {
        EXPECT_EQ(res.first, static_cast<int>(__HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED)));
        EXPECT_EQ(res.second, "emulator-check whpx commands require administrator privilege.");
    } else {
        EXPECT_THAT(res.second,
                    ::testing::HasSubstr("Feature state of Windows Hypervisor Platform"));
    }
}

TEST(WhpxTest, enableWHPX) {
    auto res = android::enableWHPX();
    if (res.first != 0) {
        EXPECT_EQ(res.first, static_cast<int>(__HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED)));
        EXPECT_EQ(res.second, "emulator-check whpx commands require administrator privilege.");
    } else {
        EXPECT_THAT(
                res.second,
                ::testing::HasSubstr("Windows Hypervisor Platform is enabled in Windows Features"));
    }
}

TEST(WhpxTest, disableWHPX) {
    auto res = android::disableWHPX();
    if (res.first != 0) {
        EXPECT_EQ(res.first, static_cast<int>(__HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED)));
        EXPECT_EQ(res.second, "emulator-check whpx commands require administrator privilege.");
    } else {
        EXPECT_THAT(res.second,
                    ::testing::HasSubstr(
                            "Windows Hypervisor Platform is disabled in Windows Features"));
    }
}
