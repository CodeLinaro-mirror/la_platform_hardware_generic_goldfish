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

#include <cstdint>
#include <string>

namespace android::crashreport::breadcrumbs {

/**
 * @brief Formats an absolute timestamp and its offset relative to a start time.
 *
 * Example format: "16:57:18.123456 (+1.2ms)"
 *
 * @param ts_ns The absolute timestamp in nanoseconds.
 * @param start_time_ns The baseline start timestamp in nanoseconds.
 * @return std::string The formatted time string.
 */
std::string FormatEventTime(uint64_t ts_ns, uint64_t start_time_ns);

}  // namespace android::crashreport::breadcrumbs
