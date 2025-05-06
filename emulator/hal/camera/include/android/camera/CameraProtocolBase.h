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

#include <mutex>
#include <string_view>

#include "android/camera/QueryParser.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish::devices::camera {

using cable::IPlug;
using cable::SocketPtr;

struct CameraProtocolBase : public IPlug {
    CameraProtocolBase(SocketPtr socket);

  protected:
    virtual bool processQuery(std::string_view query, std::string_view params) = 0;

    void sendResponse(const bool okko, const void* response, const size_t responseSize) const;
    void sendResponse(const bool okko, const std::string_view response = {}) const;

  private:
    SocketPtr onUnplug() override;
    bool onReceive(const void* data, size_t size) override;

    SocketPtr mSocket;
    QueryParser mQueryParser;
    mutable std::mutex mSocketMtx;
};

}  // namespace goldfish::devices::camera
