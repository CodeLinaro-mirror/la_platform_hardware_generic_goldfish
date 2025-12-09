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

#include "goldfish/devices/qemud.h"

namespace goldfish::devices::fingerprint {

class FingerprintDevice : public IFingerprintDevice {
  public:
    FingerprintDevice() { VLOG(1) << "Fingerprint device has been created"; }

    void onConnect() override { VLOG(1) << "Fingerprint device has been connected"; }
    void onClose() override { VLOG(1) << "Fingerprint device has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the fingerprint device: " << data;
    }

    void send(std::string_view msg) {
        auto encoded = qemud::encodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        socket()->send(encoded);
    }

    void touch(int id) override { send(absl::StrFormat("on:%d", id)); }

    virtual void release() override { send("off"); };
};

void IFingerprintDevice::registerDevice(IConnectorRegistry* registry, EventLoop* clientLoop,
                                        EventLoop* qemuLoop) {
    registry->registerHalQemuDevice(std::string(IFingerprintDevice::serviceName), clientLoop,
                                    qemuLoop, [] { return std::make_shared<FingerprintDevice>(); });
}

}  // namespace goldfish::devices::fingerprint
