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

#include "android/camera/CameraDeviceBase.h"
#include "android/camera/GpuDetails.h"

namespace goldfish::devices::camera {

struct CameraDevice : public CameraDeviceBase {
    CameraDevice(SocketPtr socket, void* imageProvider, const CameraImageProviderVtbl* vtbl,
                 GpuDetailsPtr gpuDetails);

  protected:
    using StreamCfgs = std::vector<CameraImageProviderStreamConfig>;

    bool processQuery(const std::string_view query, const std::string_view params) override;
    void configure(const std::string_view params);
    void capture(const std::string_view params);

    // TODO: think about snapshots

    StreamCfgs mStreamCfgs;
};

}  // namespace goldfish::devices::camera
