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
#pragma once

namespace goldfish::devices::battery {
/**
 * @brief Interface for emulating a battery in the Android emulator.
 *
 * This interface provides methods to control and query the state of an emulated
 * battery. The battery is emulated as a traditional QEMU MMIO device.  Clients
 * can use this interface to interact with the battery and simulate various
 * charging and health states.
 */
class IBattery {
  public:
    /**
     * @brief Types of battery chargers.
     */
    enum class ChargerType {
        NONE,        ///< No charger connected.
        AC,          ///< AC charger connected.
        USB,         ///< USB charger connected.
        WIRELESS,    ///< Wireless charger connected.
        NUM_ENTRIES  ///< Number of charger types, not a valid type itself.
    };

    /**
     * @brief Battery health states.
     */
    enum class Health {
        GOOD,         ///< Battery is healthy.
        FAILED,       ///< Battery has failed.
        DEAD,         ///< Battery is dead.
        OVERVOLTAGE,  ///< Battery is over voltage.
        OVERHEATED,   ///< Battery is overheated.
        UNKNOWN,      ///< Battery health is unknown.
        NUM_ENTRIES   ///< Number of health states, not a valid state itself.
    };

    /**
     * @brief Battery charging status.
     */
    enum class Status {
        UNKNOWN,       ///< Charging status is unknown.
        CHARGING,      ///< Battery is currently charging.
        DISCHARGING,   ///< Battery is currently discharging.
        NOT_CHARGING,  ///< Battery is not charging.
        FULL,          ///< Battery is fully charged.
        NUM_ENTRIES    ///< Number of status entries, not a valid status itself.
    };

    virtual ~IBattery() = default;

    /**
     * @brief Sets whether the device has a battery.
     * @param hasBattery True if the device has a battery, false otherwise.
     */
    virtual void setHasBattery(bool hasBattery) = 0;

    /**
     * @brief Checks if the device has a battery.
     * @return True if the device has a battery, false otherwise.
     */
    virtual bool hasBattery() const = 0;

    /**
     * @brief Sets whether the battery is present.
     * @param isPresent True if the battery is present, false otherwise.
     */
    virtual void setPresent(bool isPresent) = 0;

    /**
     * @brief Checks if the battery is present.
     * @return True if the battery is present, false otherwise.
     */
    virtual bool isPresent() const = 0;

    /**
     * @brief Sets the charging state of the battery.
     * @param isCharging True if the battery is charging, false otherwise.
     */
    virtual void setCharging(bool isCharging) = 0;

    /**
     * @brief Checks if the battery is charging.
     * @return True if the battery is charging, false otherwise.
     */
    virtual bool isCharging() const = 0;

    /**
     * @brief Sets the charger type connected to the battery.
     * @param charger The type of charger connected.
     */
    virtual void setChargerType(ChargerType charger) = 0;

    /**
     * @brief Gets the charger type connected to the battery.
     * @return The type of charger connected.
     */
    virtual ChargerType getChargerType() const = 0;

    /**
     * @brief Sets the charge level of the battery.
     * @param percentFull The charge level as a percentage (0-100).
     */
    virtual void setChargeLevel(int percentFull) = 0;

    /**
     * @brief Gets the charge level of the battery.
     * @return The charge level as a percentage (0-100).
     */
    virtual int getChargeLevel() const = 0;

    /**
     * @brief Sets the health state of the battery.
     * @param health The health state of the battery.
     */
    virtual void setHealth(Health health) = 0;

    /**
     * @brief Gets the health state of the battery.
     * @return The health state of the battery.
     */
    virtual Health getHealth() const = 0;

    /**
     * @brief Sets the charging status of the battery.
     * @param status The charging status of the battery.
     */
    virtual void setStatus(Status status) = 0;

    /**
     * @brief Gets the charging status of the battery.
     * @return The charging status of the battery.
     */
    virtual Status getStatus() const = 0;

    // Removed instance method as it doesn't belong in the interface.  If a
    // singleton is needed, it should be managed separately.
};

IBattery* instance();
}  // namespace goldfish::devices::battery