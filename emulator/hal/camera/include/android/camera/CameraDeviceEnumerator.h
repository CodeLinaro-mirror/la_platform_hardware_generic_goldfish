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

#include <string>
#include <string_view>

#include "android/camera/CameraImageProviderRegistry.h"
#include "android/camera/CameraProtocolBase.h"

namespace goldfish::devices::camera {

using CameraImageProviderRegistryPtr = std::shared_ptr<CameraImageProviderRegistry>;

struct CameraDeviceEnumerator : public CameraProtocolBase {
    CameraDeviceEnumerator(SocketPtr socket, CameraImageProviderRegistryPtr registry);

    static std::string cameraInfoToString(size_t index, const CameraImageProviderInfo& info);

  private:
    bool processQuery(std::string_view query, std::string_view params) override;
    void list(std::string_view /*params*/) const;

    const CameraImageProviderRegistryPtr mRegistry;
};

}  // namespace goldfish::devices::camera
