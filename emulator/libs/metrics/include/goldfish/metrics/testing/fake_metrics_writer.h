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

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/metrics/metrics_writer.h"

namespace goldfish::metrics::testing {

class FakeMetricsWriter : public MetricsWriter {
  public:
    void Write(MetricsEvent event) override {
        const absl::MutexLock lock(mutex);
        events.push_back(std::move(event.as_event));
    }

    bool WaitForEvents(size_t count, absl::Duration timeout) {
        auto has_enough_events = [this, count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
            return events.size() >= count;
        };
        const absl::MutexLock lock(mutex);
        return mutex.AwaitWithTimeout(absl::Condition(&has_enough_events), timeout);
    }

    mutable absl::Mutex mutex;
    std::vector<android_studio::AndroidStudioEvent> events ABSL_GUARDED_BY(mutex);
};

}  // namespace goldfish::metrics::testing
