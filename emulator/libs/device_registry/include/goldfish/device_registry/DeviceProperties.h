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

#include <string>

namespace goldfish {

/**
 * @brief A strongly-typed key used to access a property in the DeviceRegistry.
 */
template <typename T>
struct PropertyKey {
    const char* name;
};

namespace properties {

// --- Property Definitions ---
//
// To define a new shared property, add a new `PropertyKey` here.
// The key should be an `inline constexpr` variable. The template argument
// specifies the type of the property, and the string literal is the
// unique name used for storage in the registry.

/**
 * @property AdbPort
 * @brief The property for the ADB host port.
 * @type int
 */
inline constexpr PropertyKey<int> kAdbPort = {"device.adb.port"};

// Example for adding a new property:
// inline constexpr PropertyKey<std::string> kFooBar = {"device.foo.bar"};

}  // namespace properties
}  // namespace goldfish
