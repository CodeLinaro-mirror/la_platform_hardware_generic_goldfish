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
#include "goldfish/devices/fingerprint/fingerprint_device.h"

#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/devices/qemud/qemud.h"

namespace goldfish::devices::fingerprint {

using TouchEventType = ObservableFingerprintSensor::EventType;

using TouchEventSubscription = std::unique_ptr<
        android::base::eventing::ScopedEventCallback<ObservableFingerprintSensor, TouchEventType>>;

class FingerprintDevice : public IFingerprintDevice {
  public:
    FingerprintDevice() { VLOG(1) << "Fingerprint device has been created"; }

    void onConnect() override { VLOG(1) << "Fingerprint device has been connected"; }
    void onClose() override { VLOG(1) << "Fingerprint device has been disconnected"; }
    void onReceive(std::string_view data) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the fingerprint device: " << data;
    }

    void onEvent(const TouchEventType x) {
        if (x == goldfish::avd_universe::fingerprint::kReleaseEvent) {
            send("off");
        } else {
            send(absl::StrFormat("on:%d", int(x)));
        }
    }

    void setTouchEventSubscription(TouchEventSubscription subscription) {
        mTouchEventSubscription = std::move(subscription);
    }

  private:
    void send(const std::string_view msg) {
        auto encoded = qemud::encodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        socket()->send(encoded);
    }

    TouchEventSubscription mTouchEventSubscription;
};

void IFingerprintDevice::registerDevice(ObservableFingerprintSensor* sensor,
                                        IConnectorRegistry* registry, EventLoop* clientLoop,
                                        EventLoop* qemuLoop) {
    registry->registerHalQemuDevice(
            std::string(IFingerprintDevice::serviceName), clientLoop, qemuLoop,
            [sensor](std::string_view /*args*/) {
                auto dev = std::make_shared<FingerprintDevice>();
                std::weak_ptr<FingerprintDevice> weakDev = dev;

                auto touchEventSubscription = makeScopedCallback(
                        *sensor, [weakDev = std::move(weakDev)](TouchEventType event) {
                            if (const auto dev = weakDev.lock()) {
                                dev->onEvent(event);
                            }
                        });

                dev->setTouchEventSubscription(std::move(touchEventSubscription));
                return dev;
            });
}

}  // namespace goldfish::devices::fingerprint
