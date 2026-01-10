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
#include "goldfish/devices/gps/gps_device.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "goldfish/avd_universe/gps/location.h"
#include "goldfish/devices/qemud/qemud.h"

namespace goldfish::devices::gps {

using goldfish::avd_universe::gps::Location;
using goldfish::avd_universe::gps::ObservableLocation;
using LocationUpdateSubscription =
        std::unique_ptr<android::base::eventing::ScopedEventCallback<ObservableLocation, Location>>;

class GpsDevice : public IGpsDevice {
  public:
    GpsDevice() { VLOG(1) << "Gps device has been created"; }

    void OnConnect() override { VLOG(1) << "Gps device has been connected"; }
    void OnClose() override { VLOG(1) << "Gps device has been disconnected"; }
    void OnReceive(std::string_view data) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the Gps device: " << data;
    }

    void Send(const Location& location) {
        VLOG(1) << "Setting the location to:" << location;

        constexpr double kAccuracyMeters = 1;
        constexpr double kAccuracySpeed = 0.5;
        constexpr double kAccuracyHeading = 2;
        constexpr int kUnused = 0;

        // Get timestamp in milliseconds
        const uint64_t timestamp_ms = absl::ToUnixMicros(absl::Now()) / 1000;

        // Format must match:
        // https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/master/gnss/common/utils/default/FixLocationParser.cpp
        SendImpl(absl::StrFormat("$GnssRpcV1,%d,%g,%g,%g,%g,%g,%g,%lld,%g,%g,%d", kUnused,
                                 location.latitude, location.longitude, location.altitude,
                                 location.speed, kAccuracyMeters, location.bearing, timestamp_ms,
                                 kAccuracySpeed, kAccuracyHeading, kUnused));
    }

    void SetLocationUpdateSubscription(LocationUpdateSubscription s) {
        location_update_subscription_ = std::move(s);
    }

  private:
    void SendImpl(std::string_view msg) {
        auto encoded = qemud::EncodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        Socket()->Send(std::move(encoded));
    }

    LocationUpdateSubscription location_update_subscription_;
};

void IGpsDevice::RegisterDevice(ObservableLocation* observable_location,
                                IConnectorRegistry* registry, EventLoop* client_loop,
                                EventLoop* qemu_loop) {
    registry->RegisterHalQemuDevice(
            std::string(GpsDevice::kServiceName), client_loop, qemu_loop,
            [observable_location](std::string_view /*args*/) {
                auto dev = std::make_shared<GpsDevice>();
                std::weak_ptr<GpsDevice> weak_dev = dev;

                auto location_update_subscription = makeScopedCallback(
                        *observable_location,
                        [weak_dev = std::move(weak_dev)](const Location& location) {
                            if (const auto dev = weak_dev.lock()) {
                                dev->Send(location);
                            }
                        });

                dev->SetLocationUpdateSubscription(std::move(location_update_subscription));

                return dev;
            });
}

}  // namespace goldfish::devices::gps
