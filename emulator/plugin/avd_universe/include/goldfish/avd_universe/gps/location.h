// Copyright 2025 The Android Open Source Project
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
#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/eventing/observable_value.h"

namespace goldfish::avd_universe::gps {

/**
 * @brief Represents a simulated GNSS location fix.
 *
 * Defaults to Googleplex (Mountain View, CA) with an active 8-satellite fix.
 * Setting `satellites` to 0 simulates complete loss of GPS fix.
 */
struct Location {
    static constexpr double kDefaultLatitude = 37.4220186;
    static constexpr double kDefaultLongitude = -122.0839727;
    static constexpr double kDefaultAltitudeMeters = 10.0;
    static constexpr int32_t kDefaultSatellites = 8;

    double latitude = kDefaultLatitude;        //< Latitude in degrees [-90.0, 90.0].
    double longitude = kDefaultLongitude;      //< Longitude in degrees [-180.0, 180.0].
    double speed = 0.0;                        //< Speed in meters per second (>= 0.0).
    double bearing = 0.0;                      //< Bearing in degrees, 0=North, 90=East.
    double altitude = kDefaultAltitudeMeters;  //< Altitude in meters above WGS 84 ellipsoid.
    double accuracy = 0.0;                     //< Horizontal accuracy radius in meters (>= 0.0).
    int32_t satellites = kDefaultSatellites;   //< Number of satellites used for the fix (>= 0).

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Location& l) {
        absl::Format(&sink,
                     "Latitude: %.6f, Longitude: %.6f, Speed: %.2f m/s, Bearing: %.2f deg, "
                     "Altitude: %.2f m, Accuracy: %.2f m, Satellites: %d",
                     l.latitude, l.longitude, l.speed, l.bearing, l.altitude, l.accuracy,
                     l.satellites);
    }
};

archive::IWriter& operator<<(archive::IWriter&, const Location&);
absl::Status ReadValue(archive::IReader&, Location&);

using ObservableLocation =
        eventing::ObservableValue<Location, eventing::ObservableValueTriggerAlways>;

}  // namespace goldfish::avd_universe::gps