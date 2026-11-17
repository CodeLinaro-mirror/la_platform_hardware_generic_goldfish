// Copyright (C) 2026 The Android Open Source Project
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

namespace goldfish::grpc::v2 {

// Display limits that DisplayService enforces and CapabilitiesService advertises. Both read
// them from here so the advertised and enforced values cannot drift apart.

// Density used when neither the display nor the AVD hardware profile sets one.
inline constexpr int32_t kDefaultDensityDpi = 420;

// Maximum number of active displays, including the primary display.
inline constexpr int32_t kMaxSupportedDisplayCount = 4;

inline constexpr bool kSupportsVirtualDisplayCreation = true;

// UpdateDisplay rejects width, height and density_dpi changes because resizing does not reach
// the guest yet.
inline constexpr bool kSupportsDynamicResizing = false;

}  // namespace goldfish::grpc::v2
