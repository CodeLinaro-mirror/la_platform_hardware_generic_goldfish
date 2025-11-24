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

#include "goldfish/devices/qemud.h"
#include "goldfish/hal/common/emulator_reset.h"

namespace goldfish::devices::boot {

// User-defined literal for creating PropertyName objects.
IBootPropertiesDevice::PropertyName operator""_bps(const char* c_str, size_t len) {
    return IBootPropertiesDevice::PropertyName(c_str);
}
class BootPropertiesDevice : public IBootPropertiesDevice {
  public:
    BootPropertiesDevice(Properties properties, EmulatorResetCallbacks resetCallbacks)
            : mProperties(std::move(properties))
            , mResetCallbacks(resetCallbacks)
            , mQemudParser([this](const void* data, size_t size) {
                return handleMessage(std::string_view(static_cast<const char*>(data), size));
            }) {
        VLOG(1) << "BootProperties device has been created";
        if (mResetCallbacks.do_register) {
            mResetCallbacks.do_register(BootPropertiesDevice::QEMUResetHandler, this);
        }
    }

    ~BootPropertiesDevice() override {
        handleResetEvent();
        if (mResetCallbacks.do_unregister) {
            mResetCallbacks.do_unregister(BootPropertiesDevice::QEMUResetHandler, this);
        }
    }

    void send(std::string_view msg) {
        auto encoded = qemud::encodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        socket()->send(encoded);
    }

    bool isDataPartitionMounted() override { return mDataPartitionMounted; }

    void onConnect() override { VLOG(1) << "Bootproperties device has been connected"; }
    void onClose() override { VLOG(1) << "Bootproperties device has been disconnected"; }
    void onReceive(std::string_view data) override {
        mQemudParser.onReceive(data.data(), data.size());
    }

    bool handleMessage(std::string_view cmd) {
        if (cmd == "list") {
            for (const auto& [name, value] : mProperties) {
                send(absl::StrFormat("%s=%s", name, value));
            }
            send("\0");
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

    Properties mProperties;
    EmulatorResetCallbacks mResetCallbacks;
    qemud::Parser mQemudParser;
    bool mDataPartitionMounted{false};
};

void IBootPropertiesDevice::registerDevice(IConnectorRegistry* registry, Properties properties,
                                           EmulatorResetCallbacks resetCallbacks,
                                           EventLoop* clientLoop, EventLoop* qemuLoop) {
    registry->registerHalQemuDevice(std::string(IBootPropertiesDevice::serviceName), clientLoop,
                                    qemuLoop, [properties = std::move(properties), resetCallbacks] {
                                        return std::make_shared<BootPropertiesDevice>(
                                                std::move(properties), resetCallbacks);
                                    });
}

}  // namespace goldfish::devices::boot