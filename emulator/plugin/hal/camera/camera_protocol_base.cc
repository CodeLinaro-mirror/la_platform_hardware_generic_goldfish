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

#include "goldfish/devices/camera/camera_protocol_base.h"

#include "absl/log/log.h"

namespace goldfish::devices::camera {

CameraProtocolBase::CameraProtocolBase(SocketPtr socket) : mSocket(std::move(socket)) {}

/* Response format:
 *
 * xxxxxxxx           - size of the response (8 hex characters), including
 *                      the `ss` and `d` fields.
 *         ss         - status, "ok" or "ko" (for errors).
 *           d        - ':' for responses with data, zero byte otherwise.
 *            r.....r - response data (e.g. an error message).
 */
void CameraProtocolBase::sendResponse(const bool okko, const void* response,
                                      const size_t responseSize) const {
    std::lock_guard<std::mutex> lock(mSocketMtx);
    if (mSocket) {
        char prefix[8 + 2 + 1];
        ::snprintf(prefix, sizeof(prefix), "%08zx%s", responseSize + 3, (okko ? "ok" : "ko"));
        prefix[sizeof(prefix) - 1] = responseSize ? ':' : 0;

        mSocket->SendAsync(prefix, sizeof(prefix));
        if (responseSize) {
            mSocket->SendAsync(response, responseSize);
        }
    }
}

void CameraProtocolBase::sendResponse(const bool okko, const std::string_view response) const {
    if (!okko) {
        LOG(WARNING) << "camera: " << response;
    }
    sendResponse(okko, response.data(), response.size());
}

SocketPtr CameraProtocolBase::OnUnplug() {
    std::lock_guard<std::mutex> lock(mSocketMtx);
    return std::move(mSocket);
}

bool CameraProtocolBase::OnReceive(const void* data, size_t size) {
    mQueryParser.recv(data, size,
                      [this](const std::string_view query, const std::string_view params) {
                          if (!processQuery(std::move(query), std::move(params))) {
                              LOG(WARNING) << "camera: unexpected query: '" << query << "'";
                          }
                      });

    return true;
}

}  // namespace goldfish::devices::camera
