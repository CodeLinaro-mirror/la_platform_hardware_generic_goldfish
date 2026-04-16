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

#include <cstdint>

namespace android::base {

// CPU usage tracking
struct CpuTime {
    uint64_t wall_time_us = 0;
    uint64_t user_time_us = 0;
    uint64_t system_time_us = 0;

    float Usage() const {
        if (!wall_time_us) return 0.0f;
        return static_cast<float>(user_time_us + system_time_us) / static_cast<float>(wall_time_us);
    }

    CpuTime operator-(const CpuTime& other) {
        CpuTime res(*this);
        res.wall_time_us -= other.wall_time_us;
        res.user_time_us -= other.user_time_us;
        res.system_time_us -= other.system_time_us;
        return res;
    }
};

}  // namespace android::base
