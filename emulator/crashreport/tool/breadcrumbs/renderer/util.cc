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
#include "android/crashreport/breadcrumbs/util.h"

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace android::crashreport::breadcrumbs {

std::string FormatEventTime(uint64_t ts_ns, uint64_t start_time_ns) {
    DCHECK_GE(ts_ns, start_time_ns);
    const uint64_t rel_ns = ts_ns - start_time_ns;
    absl::Time t_abs = absl::FromUnixNanos(ts_ns);
    std::string abs_str = absl::FormatTime("%H:%M:%E6S", t_abs, absl::UTCTimeZone());
    std::string rel_str = absl::StrFormat("+%v", absl::Nanoseconds(rel_ns));
    return absl::StrFormat("%s (%s)", abs_str, rel_str);
}

}  // namespace android::crashreport::breadcrumbs
