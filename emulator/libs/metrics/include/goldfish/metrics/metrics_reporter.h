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

#include <memory>
#include <queue>
#include <string>
#include <thread>

#include "absl/synchronization/mutex.h"

#include "goldfish/metrics/metrics_writer.h"
#include "goldfish/metrics/uuid.h"
#include "studio_stats_wrapper.h"

namespace goldfish::metrics {

class MetricsReporter final {
  public:
    MetricsReporter() : MetricsReporter(Uuid::Generate()) {}
    explicit MetricsReporter(Uuid session_id);

    ~MetricsReporter();
    MetricsReporter(const MetricsReporter&) = delete;
    MetricsReporter(MetricsReporter&&) = delete;
    MetricsReporter& operator=(const MetricsReporter&) = delete;
    MetricsReporter& operator=(MetricsReporter&&) = delete;

    void SetWriter(std::unique_ptr<MetricsWriter> writer);

    using Callback = std::function<void(android_studio::AndroidStudioEvent&)>;
    void Report(const Callback& callback);

    const std::string& session_id() const { return session_id_; }

  private:
    void SetBaseFields(android_studio::AndroidStudioEvent& event);

    void Run();

    const std::string session_id_;

    absl::Mutex mutex_;
    std::unique_ptr<MetricsWriter> writer_ ABSL_GUARDED_BY(mutex_);
    bool running_ ABSL_GUARDED_BY(mutex_) = false;
    std::queue<MetricsEvent> event_queue_ ABSL_GUARDED_BY(mutex_);
    std::thread worker_thread_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace goldfish::metrics
