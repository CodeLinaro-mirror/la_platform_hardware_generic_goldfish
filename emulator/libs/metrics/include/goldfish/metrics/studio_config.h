/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "goldfish/file/file.h"

namespace goldfish::metrics::studio {

/**
 * Returns the path to the directory where metrics data is spooled.
 */
fs::path GetSpoolDirectory(const fs::path& user_directory);

/**
 * Tristate for user metrics opt-in.
 */
enum class OptInState {
    kUnknown,
    kOptedIn,
    kOptedOut,
};

OptInState GetUserMetricsOptIn(const fs::path& user_directory);

std::string GetMetricsUserId(const fs::path& user_directory);

}  // namespace goldfish::metrics::studio
