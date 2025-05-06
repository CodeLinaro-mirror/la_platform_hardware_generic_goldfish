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

#include "android/camera/CameraImageProviderRegistry.h"

#include <charconv>

namespace goldfish::devices::camera {

const CameraImageProviderInfoCpp* CameraImageProviderRegistry::operator[](
        const size_t index) const {
    return atImpl(index);
}

CameraImageProviderInfoCpp* CameraImageProviderRegistry::operator[](const size_t index) {
    return const_cast<CameraImageProviderInfoCpp*>(atImpl(index));
}

void CameraImageProviderRegistry::add(CameraImageProviderInfoCpp info) {
    mInfos.push_back(std::move(info));
}

void CameraImageProviderRegistry::add(CameraImageProviderInfo& info) {
    mInfos.emplace_back(info);
}

void CameraImageProviderRegistry::addStatic(void* that, CameraImageProviderInfo* src) {
    static_cast<CameraImageProviderRegistry*>(that)->add(*src);
}

void CameraImageProviderRegistry::clear() {
    mInfos.clear();
}

const CameraImageProviderInfoCpp* CameraImageProviderRegistry::atImpl(const size_t index) const {
    if (index >= mInfos.size()) {
        return nullptr;
    }

    const CameraImageProviderInfoCpp& info = mInfos[index];
    return info.getInfo().vtbl ? &info : nullptr;
}

}  // namespace goldfish::devices::camera
