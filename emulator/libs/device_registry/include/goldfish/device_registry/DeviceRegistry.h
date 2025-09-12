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

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/device_registry/DeviceProperties.h"

namespace goldfish {

/**
 * @class DeviceRegistry
 * @brief A thread-safe singleton that provides secure, write-once access
 *        to shared properties.
 *
 * ## Rationale: Preventing Uncontrolled State Modification
 *
 * The previous emulator architecture suffered from critical bugs stemming from
 * the use of a global, mutable struct for shared state. This led to two primary
 * categories of difficult-to-diagnose failures:
 *
 * 1.  **Multiple Initialization Paths:** A single property could be written to
 *     by multiple components. This created subtle race conditions and bugs
 *     where the final state of a property depended entirely on the unpredictable
 *     order of component initialization.
 * 2.  **Lack of Access Control:** Any component could modify any property at
 *     any time. A bug in a seemingly unrelated component could corrupt critical
 *     shared state (e.g., the ADB port), leading to catastrophic failures in
 *     other parts of the system.
 *
 * The `DeviceRegistry` is architected specifically to solve these two problems
 * by enforcing a strict, "write-once" policy.
 */
class DeviceRegistry {
  public:
    /**
     * @brief Sets the value of a property in the registry if it has not
     *        already been set.
     *
     * This method is the only way to write a value to the registry. It will
     * only succeed the first time it is called for a given `PropertyKey`.
     *
     * @tparam T The C++ type of the property.
     * @param key The `PropertyKey` for the property to be set.
     * @param value The value to store.
     * @return `true` if the value was successfully set, `false` otherwise.
     *
     * @code
     *   auto& registry = goldfish::DeviceRegistry::get();
     *   if (registry.setOnce(goldfish::properties::kAdbPort, 5555)) {
     *     // The value was set successfully.
     *   } else {
     *     // Another component already set the value.
     *   }
     * @endcode
     */
    template <typename T>
    bool setOnce(PropertyKey<T> key, T value) {
        absl::MutexLock lock(&m_mutex);
        auto it = m_properties.find(key.name);
        if (it == m_properties.end()) {
            m_properties[key.name] = std::move(value);
            return true;
        }
        LOG(WARNING) << "Property name " << key.name << " already set, ignoring: " << value;
        return false;
    }

    /**
     * @brief Retrieves the value of a property from the registry.
     *
     * This method is strongly typed and thread-safe. The return type is an
     * `std::optional` of the type associated with the provided `PropertyKey`.
     * If the property has not been set, or if a type mismatch somehow
     * occurred, it returns an empty optional.
     *
     * @tparam T The C++ type of the property.
     * @param key The `PropertyKey` for the property to be retrieved.
     * @return An `std::optional<T>` containing the value, or `std::nullopt`.
     *
     * @code
     *   auto& registry = goldfish::DeviceRegistry::get();
     *   std::optional<int> port = registry.get(goldfish::properties::kAdbPort);
     *   if (port) {
     *     printf("ADB port is %d\n", *port);
     *   } else {
     *     printf("ADB port has not been set.\n");
     *   }
     * @endcode
     */
    template <typename T>
    std::optional<T> get(PropertyKey<T> key) const {
        absl::MutexLock lock(&m_mutex);
        auto it = m_properties.find(key.name);
        if (it != m_properties.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                LOG(ERROR) << "Failed to cast stored property.. This should not happen for: "
                           << key.name;
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Provides access to the singleton instance of the DeviceRegistry.
     * @return A reference to the singleton.
     */
    static DeviceRegistry& get();

    /**
     * @brief Creates a new, isolated DeviceRegistry instance for testing.
     * @return A `std::unique_ptr` to a new `DeviceRegistry`.
     */
    static std::unique_ptr<DeviceRegistry> testRegistry();

  private:
    DeviceRegistry() = default;

    // Mutex to protect the internal map from concurrent access.
    mutable absl::Mutex m_mutex;

    // The internal storage. `std::any` allows us to store heterogeneous types.
    std::unordered_map<std::string, std::any> m_properties;
};

}  // namespace goldfish
