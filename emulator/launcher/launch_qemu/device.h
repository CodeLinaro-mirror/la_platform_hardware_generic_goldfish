
// Copyright (C) 2024 The Android Open Source Project
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

#include <string>
#include <vector>

#include "absl/status/status.h"

#include "emulator_config.h"

namespace android::goldfish {

/**
 * @brief Abstract base class representing a "device" the Goldfish emulator.
 */
class Device {
  public:
    /**
     * @brief Constructor.
     * @param id Unique ID for the device.
     */
    explicit Device(std::string id) : mId(id) {}
    virtual ~Device() = default;

    /**
     * @brief Returns QEMU parameters for configuring this device.
     * @param emulator Pointer to the parent Emulator.
     * @return A vector of QEMU parameter strings.
     */
    virtual std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const = 0;

    /**
     * @brief Prepares the device for emulation.
     * @param emulator Pointer to the parent Emulator.
     * @return absl::Status indicating success or failure.
     */
    virtual absl::Status initialize(const EmulatorConfig& emulator) = 0;

    /**
     * @brief Clears the device's persistent state and prepares it for
     * re-initialization.
     *
     * This method erases any persistent state associated with the device. This is
     * analogous to formatting a disk drive or resetting a device to factory
     * defaults.
     */
    virtual void clear() {}

    /**
     * @brief Returns the device's unique ID.
     * @return The device's ID.
     */
    std::string id() const { return mId; };

  protected:
    std::string mId;
};

/**
 * @brief Represents a device on the PCI bus.
 */
class PciDevice : public Device {
  public:
    /**
     * @brief Constructor.
     * @param id Unique ID of the device.
     * @param addr PCI slot and optional function number (e.g., "06.0")
     */
    explicit PciDevice(std::string id, std::string addr = "") : Device(id), mAddr(addr) {}

    /**
     * @brief Returns the PCI address, specifying slot and function number.
     * @return PCI address string.
     */
    std::string_view addr() const { return mAddr; }

  private:
    std::string mAddr;
};

}  // namespace android::goldfish
