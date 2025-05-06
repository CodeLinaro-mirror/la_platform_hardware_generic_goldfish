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

#include "android/camera/CameraDeviceEnumerator.h"

namespace goldfish::devices::camera {
using namespace std::literals;

CameraDeviceEnumerator::CameraDeviceEnumerator(SocketPtr socket,
                                               CameraImageProviderRegistryPtr registry)
        : CameraProtocolBase(std::move(socket)), mRegistry(std::move(registry)) {}

bool CameraDeviceEnumerator::processQuery(const std::string_view query,
                                          const std::string_view params) {
    if (query == "list"sv) {
        list(params);
        return true;
    } else {
        return false;
    }
}

void CameraDeviceEnumerator::list(std::string_view /*params*/) const {
    const CameraImageProviderRegistry::Infos& infos = mRegistry->enumerate();

    std::string response;
    if (infos.empty()) {
        response = "\n"s;
    } else {
        for (size_t i = 0; i < infos.size(); ++i) {
            response += cameraInfoToString(i, infos[i].getInfo());
        }
    }

    sendResponse(true, response);
}

std::string CameraDeviceEnumerator::cameraInfoToString(const size_t index,
                                                       const CameraImageProviderInfo& info) {
    const size_t supportedFrameSizesNum = info.supportedFrameSizesNum;
    if (!supportedFrameSizesNum) {
        return {};
    }

    char buf[256];
    int len = ::snprintf(buf, sizeof(buf), "name=%zu dir=%s framedims=%ux%u", index,
                         (info.isBackFacing ? "back" : "front"), info.supportedFrameSizes[0].width,
                         info.supportedFrameSizes[0].height);
    if (len <= 0) {
        return {};
    }

    std::string str(buf, len);
    for (unsigned i = 1; i < supportedFrameSizesNum; ++i) {
        len = ::snprintf(buf, sizeof(buf), ",%ux%u", info.supportedFrameSizes[i].width,
                         info.supportedFrameSizes[i].height);
        if (len <= 0) {
            return {};
        }
        str.append(buf, len);
    }
    str.push_back('\n');

    return str;
}

}  // namespace goldfish::devices::camera
