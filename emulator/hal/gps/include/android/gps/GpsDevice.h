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

#include <string_view>

#include "absl/status/status.h"

#include "aemu/base/events/EventSources.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::gps {

using android::base::eventing::CallbackEventSource;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using namespace std::string_view_literals;

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

/**
 * @brief Interface for emulating a GPS device in the Android emulator.
 *
 * This interface defines the methods for interacting with a simulated GPS
 * device. Location updates are sent over a vsock using the GnssRpcV1 protocol.
 * Location change events are sent to registered listeners.
 *
 * To receive notifications when the location is updated, you can register
 * a callback or an event listener.
 *
 * **1. Using a Callback:**
 *
 * ```c++
 * // Assuming 'gpsDevice' is a valid pointer to an IGpsDevice instance.
 * auto callbackId = gpsDevice->addCallback(
 *    (const Location& data) {
 *         // This lambda function will be called when the GPS location changes.
 *         LOG(INFO) << "New location: " << data; // Use 'data', not 'location'
 *     });
 *
 * //... later, to remove the callback:
 * gpsDevice->removeCallback(callbackId);
 * ```
 *
 * **2. Using an Event Listener:**
 *
 * ```c++
 * class MyGpsListener: public EventListener<Location> {
 * public:
 *     void eventArrived(const Location& data) override {
 *         LOG(INFO) << "New location: " << data; // Use 'data', not 'location'
 *     }
 * };
 *
 * MyGpsListener listener;
 * gpsDevice->addListener(&listener);
 *
 * //... later, to remove the listener:
 * gpsDevice->removeListener(&listener);
 * ```
 *
 * The guest HAL implementation resides in
 * `device/generic/goldfish/hals/gnss/GnssHwConn.cpp`.
 */
class IGpsDevice : public IPlug, public CallbackEventSource<Location> {
  public:
    virtual ~IGpsDevice() = default;

    /**
     * @brief QEMU service name for the GPS device.
     */
    static constexpr std::string_view serviceName = "gps"sv;

    /**
     * @brief Sets the current location of the device.
     *
     * This method updates the simulated GPS location and sends a location change
     * event to all registered listeners.
     *
     * @param location The new GPS location.
     */
    virtual void setLocation(const Location& location) = 0;

    /**
     * @brief Gets the last known location that was set.
     *
     * This method retrieves the most recently set GPS location.
     *
     * @return The last known GPS location.
     */
    virtual Location getLocation() const = 0;

    /**
     * @brief Registers the GPS device with the connector registry.
     *
     * This function registers the GPS device with the provided \p registry
     * instance, making it available for connection through the qemud pipe.
     *
     * @param registry The connector registry instance.
     */
    static void registerDevice(IConnectorRegistry* registry);
};

}  // namespace goldfish::devices::gps