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

#include "goldfish/eventing/observable_value.h"

namespace goldfish::avd_universe::gps {

/**
 * @brief Represents a GPS location.
 *
 * This struct holds information about a GPS location, including latitude,
 * longitude, speed, bearing, altitude, and the number of satellites used
 * to acquire the fix.
 */
struct Location {
    double latitude;     //< Latitude in degrees.
    double longitude;    //< Longitude in degrees.
    double speed;        //< Speed in meters per second.
    double bearing;      //< Bearing in degrees, 0=North, 90=East.
    double altitude;     //< Altitude in meters above WGS 84 ellipsoid.
    int32_t satellites;  //< Number of satellites used for the fix.

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Location& l) {
        absl::Format(&sink,
                     "Latitude: %.6f, Longitude: %.6f, Speed: %.2f m/s, Bearing: %.2f deg, "
                     "Altitude: %.2f m, Satellites: %d",
                     l.latitude, l.longitude, l.speed, l.bearing, l.altitude, l.satellites);
    }
};

using ObservableLocation =
        eventing::ObservableValue<Location, eventing::ObservableValueTriggerAlways>;

}  // namespace goldfish::avd_universe::gps