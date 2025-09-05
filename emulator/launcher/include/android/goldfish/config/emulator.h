
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

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "android/cmdline-option.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/devices/device.h"

#include "input_paths.h"

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
     * @param opts The android options to use for the emulator.
     */
    explicit Emulator(ResolvedInputPaths resolved_paths, std::unique_ptr<Avd> avd, AndroidOptions opts) : mResolvedPaths(resolved_paths), mAvd(std::move(avd)), mOpts(opts) {}

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

    /**
     * @brief Adds a device of type T to the emulator.
     *
     * This function creates a new device of the specified type `T`
     * using the provided arguments `args` and adds it to the emulator's
     * device list (`mDevices`) and device map (`mDeviceMap`).
     * The device's ID is used as the key in the device map.
     *
     * Note that the order in which you call this matters. If device A is
     * added before B than A will be initialized before B.
     *
     * Inserting a device with the same id twice will result in a fatal error.
     *
     * @tparam T The type of the device to add. Must be a subclass of `Device`.
     * @tparam Args The types of the arguments to pass to the device constructor.
     * @param args The arguments to pass to the device constructor.
     */
    template <typename T, typename... Args>
    void addDevice(Args&&... args) {
        auto newDevice = std::make_unique<T>(std::forward<Args>(args)...);
        auto result = mDeviceMap.insert({newDevice->id(), newDevice.get()});
        if (!result.second) {
            LOG(FATAL) << "Device with id " << newDevice->id() << " already exists.";
        }
        mDevices.push_back(std::move(newDevice));
    }

    // The resolved paths to input binaries and data
    const ResolvedInputPaths& paths() const { return mResolvedPaths; }

    // The avd description used to configure this emulator
    const Avd& avd() const { return *mAvd; }

    // The android options used to configure this emulator
    const AndroidOptions& opts() const { return mOpts; }

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
    absl::Status addDevices();

    // Constructs the qemu command line.
    std::string qemu_exe_path() const;
    std::vector<std::string> getCmdline() const;
    std::string mVmodule;

    const ResolvedInputPaths mResolvedPaths;
    const std::unique_ptr<Avd> mAvd;
    const AndroidOptions mOpts;
    std::vector<std::unique_ptr<Device>> mDevices;
    std::unordered_map<std::string, Device*> mDeviceMap;
};
}  // namespace android::goldfish
