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
#include "android/boot/BootPropertiesDevice.h"

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

namespace goldfish::devices::boot {

class BootPropertiesDevice : public IBootPropertiesDevice {
  public:
    BootPropertiesDevice(SocketPtr socket, Properties properties,
                         RegisterEmulatorReset registerEmulatorReset)
        : mSocket(std::move(socket)), mProperties(std::move(properties)) {
        VLOG(1) << "BootProperties device has been created";
        registerEmulatorReset(BootPropertiesDevice::QEMUResetHandler, this);
    }

    ~BootPropertiesDevice() {}
    SocketPtr onUnplug() override { return std::move(mSocket); }

    void send(std::string_view msg) {
        goldfish::devices::qemud::sendAsync(msg.data(), msg.size(), *mSocket.get());
    }

    bool isDataPartitionMounted() override { return mDataPartitionMounted; }

    bool onReceive(const void* data, size_t size) override {
        std::string_view cmd = std::string_view((char*)data, size);
        if (cmd == "list") {
            for (const auto& [name, value] : mProperties) {
                send(absl::StrFormat("%s=%s", name, value));
            }
            mDataPartitionMounted = true;
            fireEvent({.dataPartitionMounted = true});
        }
        return true;
    }

  private:
    static void QEMUResetHandler(void* opaque) {
        auto device = static_cast<BootPropertiesDevice*>(opaque);
        device->handleResetEvent();
    }

    void handleResetEvent() {
        {
            mDataPartitionMounted = false;
        }
        fireEvent({.dataPartitionMounted = false});
    }

    SocketPtr mSocket;
    Properties mProperties;
    bool mDataPartitionMounted{false};
};

void IBootPropertiesDevice::registerDevice(IConnectorRegistry* registry, Properties properties,
                                           RegisterEmulatorReset registerEmulatorReset) {
    registry->registerQemuDevice(
            std::string(IBootPropertiesDevice::serviceName),
            [properties = std::move(properties),
             registerEmulatorReset = std::move(registerEmulatorReset)](
                    SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                    std::string_view args) {
                return std::make_shared<BootPropertiesDevice>(
                        std::move(socket), std::move(properties), std::move(registerEmulatorReset));
            });
}

}  // namespace goldfish::devices::boot