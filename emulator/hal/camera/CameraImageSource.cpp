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

#include "android/camera/CameraImageSource.h"

#include "absl/log/log.h"

using namespace std::literals;

namespace goldfish::devices::camera {

CameraImageSource getCameraImageSourceFromName(const std::string_view name) {
    if (name == "emulated"sv) {
        return CameraImageSource::EMULATED;
    } else if (name.starts_with("webcam"sv)) {
        return CameraImageSource::WEBCAM;
    } else if (name == "virtualscene"sv) {
        return CameraImageSource::VIRTUALSCENE;
    } else if (name == "videofile"s) {
        return CameraImageSource::VIDEOFILE;
    } else if (name == "imagefile"s) {
        return CameraImageSource::IMAGEFILE;
    } else if ((name != "none"sv) && !name.empty()) {
        LOG(WARNING) << "camera: unexpected camera source: '" << name << "'";
    }

    return CameraImageSource::NONE;
}

}  // namespace goldfish::devices::camera
