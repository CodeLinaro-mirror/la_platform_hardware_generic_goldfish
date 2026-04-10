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

#include <string_view>
#include <vector>

#include "goldfish/devices/camera/camera_image_provider_info_cpp.h"

namespace goldfish::devices::camera {

struct CameraImageProviderRegistry {
    using Infos = std::vector<CameraImageProviderInfoCpp>;

    const Infos& enumerate() const { return mInfos; }
    const CameraImageProviderInfoCpp* operator[](size_t index) const;
    CameraImageProviderInfoCpp* operator[](size_t index);

    void add(CameraImageProviderInfoCpp info);
    void add(CameraImageProviderInfo& info);
    static void addStatic(void* that, CameraImageProviderInfo* src);

    void clear();

  private:
    const CameraImageProviderInfoCpp* atImpl(size_t index) const;
    std::vector<CameraImageProviderInfoCpp> mInfos;
};

}  // namespace goldfish::devices::camera
