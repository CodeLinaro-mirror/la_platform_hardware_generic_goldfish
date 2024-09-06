
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
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"

#include "android/goldfish/config/avd.h"

namespace android::goldfish {

class Device;

// Represents an emulator that can launch qemu with the proper parameters based
// on an avd.
class Emulator {
  public:
    /**
     * @brief Constructs an emulator with the given avd and optional additional
     * parameters.
     *
     * @param avd The AVD configuration to use for the emulator.
     * @param logLevel The minimum logging level to use, should be between 0 to 4.
     * @param vmodules Per-module log verbosity levels.
     * @param additionalParams Additional parameters to pass to the QEMU command
     * line. These parameters will be appended to the default command line
     * generated from the AVD configuration.
     */
    explicit Emulator(Avd avd, int logLevel, std::string vmodules,
                      std::vector<std::string> additionalParams = {});

    /**
     * @brief Retrieves a device driver of a specified type.
     * @tparam T The specific Device subclass type.
     * @param id Identifier of the device to retrieve.
     * @return Pointer to the requested device (as type T), or nullptr if not
     * found.
     */
    template <typename T>
    T* get(const std::string& id) const {
        static_assert(std::is_base_of_v<Device, T>, "T must be a subclass of Device");
        auto res = mDeviceMap.find(id);
        if (res == mDeviceMap.end()) {
            return nullptr;
        }
        return static_cast<T*>(res->second);
    }

    // The avd description used to configure this emulator
    const Avd& avd() const { return mAvd; }

    /**
     * @brief Clears the device's persistent state and prepares it for
     * re-initialization.
     *
     * This method erases any persistent state associated with the device. This is
     * analogous to formatting a disk drive or resetting a device to factory
     * defaults.
     */
    void clear();

    /**
     * @brief Initializes and prepares the emulator and its devices for launch.
     *
     * This step involves device validation and potential setup actions, such
     * as creating backends (e.g., disk images).
     * @return absl::Status indicating success or failure.
     */
    absl::Status initialize();

    /**
     * @brief Launches the emulator using the configured QEMU command line.
     *
     * This method starts the QEMU process and logs its output (stdout/stderr).
     * @return absl::Status indicating success or failure.
     */
    absl::Status launch();

  private:
    // Constructs the qemu command line.
    std::vector<std::string> getCmdline() const;
    std::string mVmodule;

    const Avd mAvd;
    std::vector<std::unique_ptr<Device>> mDevices;
    std::unordered_map<std::string, Device*> mDeviceMap;
};
}  // namespace android::goldfish
