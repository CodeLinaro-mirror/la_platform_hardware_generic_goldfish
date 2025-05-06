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

#pragma once

#include "android/camera/CameraImageProvider.h"

namespace goldfish::devices::camera {

struct CameraImageProviderInfoCpp {
    ~CameraImageProviderInfoCpp();

    CameraImageProviderInfoCpp();
    CameraImageProviderInfoCpp(CameraImageProviderInfoCpp&& rhs);
    CameraImageProviderInfoCpp& operator=(CameraImageProviderInfoCpp&& rhs);

    CameraImageProviderInfoCpp(CameraImageProviderInfo& src);

    CameraImageProviderInfoCpp(const CameraImageProviderInfoCpp&) = delete;
    CameraImageProviderInfoCpp& operator=(const CameraImageProviderInfoCpp& rhs) = delete;

    const CameraImageProviderInfo& getInfo() const { return mInfo; }

    void setBackFacing(bool);

  private:
    CameraImageProviderInfo mInfo;
};

}  // namespace goldfish::devices::camera
