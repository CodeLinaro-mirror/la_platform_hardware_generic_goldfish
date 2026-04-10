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

#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/avd_universe/gps/location.h"
#include "goldfish/devices/qemud/qemud.h"

namespace goldfish::devices::gps {

using goldfish::avd_universe::gps::Location;
using goldfish::avd_universe::gps::ObservableLocation;
using LocationUpdateSubscription =
        std::unique_ptr<android::base::eventing::ScopedEventCallback<ObservableLocation, Location>>;

namespace {
std::string ToNmeaCoord(double degrees, bool is_latitude) {
    const double d = std::abs(degrees);
    const int dint = static_cast<int>(d);
    const double m = (d - dint) * 60.0;
    const int mint = static_cast<int>(m);
    const int fint = static_cast<int>(10000 * (m - mint));

    if (is_latitude) {
        // latitude from 0 to 90 need 2 digits
        return absl::StrFormat("%02d%02d.%04d", dint, mint, fint);
    }
    // longitude from 0 to 180, need 3 digits
    return absl::StrFormat("%03d%02d.%04d", dint, mint, fint);
}

std::string CalculateChecksum(const std::string& sentence_content) {
    unsigned char checksum = 0;
    for (const char c : sentence_content) {
        checksum ^= c;
    }
    return absl::StrFormat("%02X", checksum);
}

std::string FormatGPGGA(const Location& loc, absl::Time now) {
    const std::string time_str = absl::FormatTime("%H%M%S", now, absl::UTCTimeZone());

    const std::string lat_str = ToNmeaCoord(loc.latitude, true);

    const char lat_dir = (loc.latitude >= 0) ? 'N' : 'S';

    const std::string lon_str = ToNmeaCoord(loc.longitude, false);
    const char lon_dir = (loc.longitude >= 0) ? 'E' : 'W';

    const int fix_quality = 1;

    const std::string body =
            absl::StrFormat("GPGGA,%s,%s,%c,%s,%c,%d,%02d,1.0,%.2f,M,0.0,M,,", time_str, lat_str,
                            lat_dir, lon_str, lon_dir, fix_quality, loc.satellites, loc.altitude);

    return absl::StrFormat("$%s*%s\r\n", body, CalculateChecksum(body));
}

std::string FormatGPRMC(const Location& loc, absl::Time now) {
    const std::string time_str = absl::FormatTime("%H%M%S", now, absl::UTCTimeZone());

    const std::string date_str = absl::FormatTime("%d%m%y", now, absl::UTCTimeZone());

    const std::string lat_str = ToNmeaCoord(loc.latitude, true);
    const char lat_dir = (loc.latitude >= 0) ? 'N' : 'S';
    const std::string lon_str = ToNmeaCoord(loc.longitude, false);
    const char lon_dir = (loc.longitude >= 0) ? 'E' : 'W';

    const double speed_knots = loc.speed * 1.94384;

    const std::string body = absl::StrFormat("GPRMC,%s,A,%s,%c,%s,%c,%.2f,%.2f,%s,0.0,W", time_str, lat_str,
                                       lat_dir, lon_str, lon_dir, speed_knots, loc.bearing, date_str);

    return absl::StrFormat("$%s*%s\r\n", body, CalculateChecksum(body));
}

constexpr Location kGoogleHqLocation{.latitude = 37.4220186,
                                    .longitude = -122.0839727,
                                    .speed = 0.0,
                                    .bearing = 0.0,
                                    .altitude = 10.0,
                                    .satellites = 8};
}  // namespace

class GpsDevice : public IGpsDevice {
  public:
    explicit GpsDevice(EventLoop* loop) : loop_(loop) { VLOG(1) << "Gps device has been created"; }

    void OnConnect() override {
        VLOG(1) << "Gps device has been connected";

        self_ = shared_from_this();
        one_second_timer_ = loop_->CreateTimer([this] { OneSecondTick(); });
        one_second_timer_->Schedule(absl::ToChronoMilliseconds(absl::Milliseconds(1000)),
                                  absl::ToChronoMilliseconds(absl::Milliseconds(1000)));
    }

    void OnClose() override {
        VLOG(1) << "Gps device has been disconnected";

        if (one_second_timer_) {
            one_second_timer_->Cancel();
        }
        if (self_) {
            loop_->Post([this] { self_.reset(); }).IgnoreError();
        }
    }

    void OnReceive(std::string_view data) override {
        VLOG(1) << "The guest is (unexpectedly) sending data to the Gps device: " << data;
    }

    void Send(const Location& location) {
        VLOG(1) << "Setting the location to:" << location;
        {
            const absl::MutexLock lock(&location_mutex_);
            location_ = location;
        }
        OneSecondTick();
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

    void OneSecondTick() {
        const absl::MutexLock lock(&location_mutex_);
        const auto loc = location_;

        const absl::Time now = absl::Now();
        const std::string gpgga = FormatGPGGA(loc, now);
        const std::string gprmc = FormatGPRMC(loc, now);
        SendImpl(gpgga);
        SendImpl(gprmc);
    }

    LocationUpdateSubscription location_update_subscription_;

    absl::Mutex location_mutex_;

    EventLoop* const loop_;
    std::shared_ptr<EventLoop::Timer> one_second_timer_;
    std::shared_ptr<IGpsDevice> self_;
    Location location_ ABSL_GUARDED_BY(location_mutex_) = {kGoogleHqLocation};
};

void IGpsDevice::RegisterDevice(ObservableLocation* observable_location,
                                IConnectorRegistry* registry, EventLoop* client_loop,
                                EventLoop* qemu_loop) {
    registry->RegisterHalQemuDevice(
            std::string(GpsDevice::kServiceName), client_loop, qemu_loop,
            [observable_location, client_loop](std::string_view /*args*/) {
                auto dev = std::make_shared<GpsDevice>(client_loop);
                std::weak_ptr<GpsDevice> weak_dev = dev;

                auto location_update_subscription = MakeScopedCallback(
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
