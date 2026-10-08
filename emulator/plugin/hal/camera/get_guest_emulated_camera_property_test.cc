/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/devices/camera/get_guest_emulated_camera_property.h"

#include <gtest/gtest.h>

using namespace std::literals;
using namespace goldfish::devices::camera;

TEST(getGuestEmulatedCameraProperty, simple) {
    EXPECT_EQ(getGuestEmulatedCameraProperty(true, true), "both"s);
    EXPECT_EQ(getGuestEmulatedCameraProperty(true, false), "front"s);
    EXPECT_EQ(getGuestEmulatedCameraProperty(false, true), "back"s);
    EXPECT_EQ(getGuestEmulatedCameraProperty(false, false), "none"s);
}
