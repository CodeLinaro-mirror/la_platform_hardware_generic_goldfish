// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "android/fingerprint/FingerprintDevice.h"

#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/qemud.h"

using goldfish::devices::PingTopic;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

namespace goldfish::devices::fingerprint {

class FingerprintDevice : public IFingerprintDevice {
  public:
    FingerprintDevice(SocketPtr socket) : mSocket(std::move(socket)) {
        VLOG(1) << "Fingerprint device has been created";
    }

    ~FingerprintDevice() {}
    SocketPtr onUnplug() override { return std::move(mSocket); }

    bool onReceive(const void* data, size_t size) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the fingerprint device: "
                << std::string_view((char*)data, size);
        return true;
    }

    void send(std::string_view msg) {
        goldfish::devices::qemud::sendAsync(msg.data(), msg.size(), *mSocket.get());
    }

    void touch(int id) override { send(absl::StrFormat("on:%d", id)); }

    virtual void release() override { send("off"); };

  private:
    SocketPtr mSocket;
};

void IFingerprintDevice::registerDevice(IConnectorRegistry* registry) {
    registry->registerQemuDevice(std::string(IFingerprintDevice::serviceName),
                                 [](SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                                    std::string_view args) {
                                     return std::make_shared<FingerprintDevice>(std::move(socket));
                                 });
}

}  // namespace goldfish::devices::fingerprint