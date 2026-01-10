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

    void OnConnect() override { VLOG(1) << "Fingerprint device has been connected"; }
    void OnClose() override { VLOG(1) << "Fingerprint device has been disconnected"; }
    void OnReceive(std::string_view data) override {
        VLOG(1) << "The guest is (unexpectedly) Sending data to the fingerprint device: " << data;
    }

    void OnEvent(const TouchEventType x) {
        if (x == goldfish::avd_universe::fingerprint::kReleaseEvent) {
            Send("off");
        } else {
            Send(absl::StrFormat("on:%d", static_cast<int>(x)));
        }
    }

    void SetTouchEventSubscription(TouchEventSubscription subscription) {
        touch_event_subscription_ = std::move(subscription);
    }

  private:
    void Send(const std::string_view msg) {
        auto encoded = qemud::EncodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        Socket()->Send(encoded);
    }

    TouchEventSubscription touch_event_subscription_;
};

void IFingerprintDevice::RegisterDevice(ObservableFingerprintSensor* sensor,
                                        IConnectorRegistry* registry, EventLoop* client_loop,
                                        EventLoop* qemu_loop) {
    registry->RegisterHalQemuDevice(
            std::string(IFingerprintDevice::kServiceName), client_loop, qemu_loop,
            [sensor](std::string_view /*args*/) {
                auto dev = std::make_shared<FingerprintDevice>();
                std::weak_ptr<FingerprintDevice> weak_dev = dev;

                auto touch_event_subscription = makeScopedCallback(
                        *sensor, [weak_dev = std::move(weak_dev)](TouchEventType event) {
                            if (const auto dev = weak_dev.lock()) {
                                dev->OnEvent(event);
                            }
                        });

                dev->SetTouchEventSubscription(std::move(touch_event_subscription));
                return dev;
            });
}

}  // namespace goldfish::devices::fingerprint
