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
#include "android/gps/GpsDevice.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/qemud.h"

using goldfish::devices::PingTopic;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

namespace goldfish::devices::gps {

class GpsDevice : public IGpsDevice {
  public:
    GpsDevice(SocketPtr socket) : mSocket(std::move(socket)) {
        VLOG(1) << "Gps device has been created";
        setLocation(googleplex());
    }

    ~GpsDevice() {}
    SocketPtr onUnplug() override { return std::move(mSocket); }

    bool onReceive(const void* data, size_t size) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the Gps device: "
                << std::string_view((char*)data, size);
        return true;
    }

    void send(std::string msg) {
        goldfish::devices::qemud::sendAsync(msg.data(), msg.size(), *mSocket.get());
    }

    Location getLocation() const override { return mLastKnownLocation; };

    void setLocation(const Location& location) override {
        VLOG(1) << "Setting the location to:" << location;

        mLastKnownLocation = location;

        constexpr double kAccuracyMeters = 1;
        constexpr double kAccuracySpeed = 0.5;
        constexpr double kAccuracyHeading = 2;
        constexpr int kUnused = 0;

        // Get timestamp in milliseconds
        uint64_t tMs = absl::ToUnixMicros(absl::Now()) / 1000;

        // Format must match:
        // https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/master/gnss/common/utils/default/FixLocationParser.cpp
        send(absl::StrFormat("$GnssRpcV1,%d,%g,%g,%g,%g,%g,%g,%lld,%g,%g,%d", kUnused,
                             location.latitude, location.longitude, location.altitude,
                             location.speed, kAccuracyMeters, location.bearing, tMs, kAccuracySpeed,
                             kAccuracyHeading, kUnused));

        fireEvent(location);
    };

  private:
    Location googleplex() {
        return Location{
                .latitude = 39.237256,
                .longitude = -123.150032,
                .speed = 0.0,
                .bearing = 0.0,
                .altitude = 0.0,
                .satellites = 4,
        };
    };

    Location mLastKnownLocation = googleplex();
    SocketPtr mSocket;
};

void IGpsDevice::registerDevice(IConnectorRegistry* registry) {
    registry->registerQemuDevice(
            std::string(IGpsDevice::serviceName),
            [](SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
               std::string_view args) { return std::make_shared<GpsDevice>(std::move(socket)); });
}

}  // namespace goldfish::devices::gps