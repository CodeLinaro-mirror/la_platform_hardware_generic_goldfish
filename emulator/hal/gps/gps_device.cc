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

    void send(const Location& location) {
        VLOG(1) << "Setting the location to:" << location;

        constexpr double kAccuracyMeters = 1;
        constexpr double kAccuracySpeed = 0.5;
        constexpr double kAccuracyHeading = 2;
        constexpr int kUnused = 0;

        // Get timestamp in milliseconds
        uint64_t tMs = absl::ToUnixMicros(absl::Now()) / 1000;

        // Format must match:
        // https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/master/gnss/common/utils/default/FixLocationParser.cpp
        sendImpl(absl::StrFormat("$GnssRpcV1,%d,%g,%g,%g,%g,%g,%g,%lld,%g,%g,%d", kUnused,
                                 location.latitude, location.longitude, location.altitude,
                                 location.speed, kAccuracyMeters, location.bearing, tMs,
                                 kAccuracySpeed, kAccuracyHeading, kUnused));
    }

    void setLocationUpdateSubscription(LocationUpdateSubscription s) {
        mLocationUpdateSubscription = std::move(s);
    }

  private:
    void sendImpl(std::string_view msg) {
        auto encoded = qemud::encodeQemudPacket(msg);
        VLOG(2) << "Sending " << encoded;
        Socket()->Send(std::move(encoded));
    }

    LocationUpdateSubscription mLocationUpdateSubscription;
};

void IGpsDevice::registerDevice(ObservableLocation* observableLocation,
                                IConnectorRegistry* registry, EventLoop* clientLoop,
                                EventLoop* qemuLoop) {
    registry->registerHalQemuDevice(
            std::string(GpsDevice::serviceName), clientLoop, qemuLoop,
            [observableLocation](std::string_view /*args*/) {
                auto dev = std::make_shared<GpsDevice>();
                std::weak_ptr<GpsDevice> weakDev = dev;

                auto locationUpdateSubscription = makeScopedCallback(
                        *observableLocation,
                        [weakDev = std::move(weakDev)](const Location& location) {
                            if (const auto dev = weakDev.lock()) {
                                dev->send(location);
                            }
                        });

                dev->setLocationUpdateSubscription(std::move(locationUpdateSubscription));

                return dev;
            });
}

}  // namespace goldfish::devices::gps
