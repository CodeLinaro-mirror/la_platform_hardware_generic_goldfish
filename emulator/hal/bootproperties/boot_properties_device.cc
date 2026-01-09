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
#include "goldfish/devices/boot/boot_properties_device.h"

#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/devices/qemud/qemud.h"

namespace goldfish::devices::boot {

class BootPropertiesDevice : public IBootPropertiesDevice {
  public:
    explicit BootPropertiesDevice(Properties properties)
            : properties_(std::move(properties))
            , qemud_parser_([this](const void* data, size_t size) {
                return HandleMessage(std::string_view(static_cast<const char*>(data), size));
            }) {
        VLOG(1) << "BootProperties device has been created";
    }

    void Send(std::string_view msg) {
        auto encoded = qemud::EncodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        Socket()->Send(encoded);
    }

    void OnConnect() override { VLOG(1) << "Bootproperties device has been connected"; }
    void OnClose() override { VLOG(1) << "Bootproperties device has been disconnected"; }
    void OnReceive(std::string_view data) override {
        qemud_parser_.OnReceive(data.data(), data.size());
    }

    bool HandleMessage(std::string_view cmd) {
        if (cmd == "list") {
            for (const auto& [name, value] : properties_) {
                Send(absl::StrFormat("%s=%s", name, value));
            }
            Send("\0");
        }
        return true;
    }

  private:
    const Properties properties_;
    qemud::Parser qemud_parser_;
};

void IBootPropertiesDevice::RegisterDevice(IConnectorRegistry* registry, Properties properties,
                                           EventLoop* client_loop, EventLoop* qemu_loop) {
    registry->RegisterHalQemuDevice(
            std::string(IBootPropertiesDevice::kServiceName), client_loop, qemu_loop,
            [properties = std::move(properties)](std::string_view /*args*/) {
                return std::make_shared<BootPropertiesDevice>(properties);
            });
}

}  // namespace goldfish::devices::boot
