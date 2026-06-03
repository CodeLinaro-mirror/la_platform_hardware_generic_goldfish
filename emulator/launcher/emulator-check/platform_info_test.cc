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

#include <gtest/gtest.h>

#include "platform_info.h"

TEST(PlatformInfoTest, getWindowManagerName) {
    std::string wm = android::getWindowManagerName();
#ifdef _WIN32
    EXPECT_EQ(wm, "Windows");
#elif defined(__APPLE__)
    EXPECT_EQ(wm, "Mac");
#else
    // On Linux it can be empty if headless, or a valid string.
    // Just verify it doesn't crash.
#endif
}

TEST(PlatformInfoTest, getDesktopEnvironmentName) {
    std::string de = android::getDesktopEnvironmentName();
#ifdef _WIN32
    EXPECT_EQ(de, "Windows");
#elif defined(__APPLE__)
    EXPECT_EQ(de, "Mac");
#else
    // On Linux it can be empty if headless, or a valid string.
    // Just verify it doesn't crash.
#endif
}
