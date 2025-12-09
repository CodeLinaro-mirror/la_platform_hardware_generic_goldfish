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

#include "goldfish/devices/camera/camera_image_source.h"

#include <gtest/gtest.h>

using namespace std::literals;
using namespace goldfish::devices::camera;

TEST(CameraImageSource, simple) {
    EXPECT_EQ(getCameraImageSourceFromName("emulated"sv), CameraImageSource::EMULATED);
    EXPECT_EQ(getCameraImageSourceFromName("virtualscene"sv), CameraImageSource::VIRTUALSCENE);
    EXPECT_EQ(getCameraImageSourceFromName("videofile"sv), CameraImageSource::VIDEOFILE);
    EXPECT_EQ(getCameraImageSourceFromName("imagefile"sv), CameraImageSource::IMAGEFILE);
}

TEST(CameraImageSource, none) {
    EXPECT_EQ(getCameraImageSourceFromName("none"sv), CameraImageSource::NONE);
    EXPECT_EQ(getCameraImageSourceFromName(""sv), CameraImageSource::NONE);
    EXPECT_EQ(getCameraImageSourceFromName("other strings"sv), CameraImageSource::NONE);
}

TEST(CameraImageSource, webcam) {
    EXPECT_EQ(getCameraImageSourceFromName("webcam0"sv), CameraImageSource::WEBCAM);
    EXPECT_EQ(getCameraImageSourceFromName("webcam1"sv), CameraImageSource::WEBCAM);
    EXPECT_EQ(getCameraImageSourceFromName("webcam42"sv), CameraImageSource::WEBCAM);
    EXPECT_EQ(getCameraImageSourceFromName("webcam also works"sv), CameraImageSource::WEBCAM);
}
