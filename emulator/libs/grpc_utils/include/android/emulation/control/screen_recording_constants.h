// Copyright 2026 The Android Open Source Project
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

namespace android::emulation::control {

constexpr int kMinVideoBitrate = 100 * 1000;           // bps
constexpr int kMaxVideoBitrate = 25 * 1000 * 1000;     // bps
constexpr int kDefaultTimeLimit = 3 * 60;              // seconds (180)
constexpr int kMaxTimeLimit = 30 * 60;                 // seconds (1800)
constexpr int kMaxFPS = 60;                            // fps
constexpr int kFPS = 24;                               // fps
constexpr int kDefaultVideoBitrate = 4 * 1000 * 1000;  // bps (4Mbps)

}  // namespace android::emulation::control
