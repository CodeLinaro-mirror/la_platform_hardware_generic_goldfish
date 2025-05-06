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

#include "android/camera/CameraImageProviderInfoCpp.h"

#include <cstdlib>
#include <utility>

#include "absl/log/log.h"

namespace goldfish::devices::camera {
namespace {
static const CameraImageProviderInfo kEmptyCameraImageProviderInfo = {
    .vtbl = nullptr,
    .supportedFrameSizes = nullptr,
    .createArg = nullptr,
    .supportedFrameSizesNum = 0,
    .isBackFacing = 0,
    .needFreeSupportedSizes = 0,
};
}

CameraImageProviderInfoCpp::~CameraImageProviderInfoCpp() {
    constexpr auto unconst = [](const void* p) { return const_cast<void*>(p); };

    if (mInfo.needFreeSupportedSizes) {
        ::free(unconst(mInfo.supportedFrameSizes));
    }

    if (mInfo.createArg) {
        const auto dctor = mInfo.vtbl->createArgDctor;
        if (dctor) {
            dctor(unconst(mInfo.createArg));
        } else {
            LOG(FATAL) << "`createArg` is provided, but there is no `createArgDctor`";
        }
    }
}

CameraImageProviderInfoCpp::CameraImageProviderInfoCpp() : mInfo(kEmptyCameraImageProviderInfo) {}

CameraImageProviderInfoCpp::CameraImageProviderInfoCpp(CameraImageProviderInfoCpp&& rhs)
        : mInfo(std::exchange(rhs.mInfo, kEmptyCameraImageProviderInfo)) {}

CameraImageProviderInfoCpp& CameraImageProviderInfoCpp::operator=(
        CameraImageProviderInfoCpp&& rhs) {
    if (this != &rhs) {
        mInfo = std::exchange(rhs.mInfo, kEmptyCameraImageProviderInfo);
    }

    return *this;
}

CameraImageProviderInfoCpp::CameraImageProviderInfoCpp(CameraImageProviderInfo& src) {
    mInfo = std::exchange(src, kEmptyCameraImageProviderInfo);
}

void CameraImageProviderInfoCpp::setBackFacing(const bool value) {
    mInfo.isBackFacing = value ? 1 : 0;
}

}  // namespace goldfish::devices::camera
