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

#include "goldfish/metrics/metrics_reporter.h"

#include "absl/log/log.h"

#include "android/base/system.h"
#include "goldfish/tools/aemu_version.h"
#include "qemu-version.h"

namespace goldfish::metrics {

MetricsReporter::MetricsReporter(Uuid session_id) : session_id_(session_id.ToString()) {
    absl::MutexLock lock(mutex_);
    running_ = true;
    worker_thread_ = std::thread(&MetricsReporter::Run, this);
    LOG(INFO) << "Metrics session started: " << session_id_;
}

MetricsReporter::~MetricsReporter() {
    {
        absl::MutexLock lock(mutex_);
        running_ = false;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    LOG(INFO) << "Metrics session stopped.";
}

void MetricsReporter::SetWriter(std::unique_ptr<MetricsWriter> writer) {
    absl::MutexLock lock(mutex_);
    writer_ = std::move(writer);
}

void MetricsReporter::SetBaseFields(android_studio::AndroidStudioEvent& event) {
    // Default to PING but most reporters should replace this.
    event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);

    // This can be overridden if needed e.g. reports for previous processes that crashed.
    event.set_studio_session_id(session_id_);

    auto& product = *event.mutable_product_details();
    auto& details = *event.mutable_emulator_details();
    product.set_product(android_studio::ProductDetails::EMULATOR);
    product.set_version(std::string(goldfish::version::GetEmulatorVersion()));
    product.set_build(std::string(goldfish::version::GetEmulatorFullVersion()));
    details.set_core_version(QEMU_FULL_VERSION);

    const auto times = android::base::System::Get()->GetProcessTimes();
    details.set_system_time(times.system_ms);
    details.set_user_time(times.user_ms);
    details.set_wall_time(times.wall_clock_ms);
}

void MetricsReporter::Report(const Callback& callback) {
    absl::MutexLock lock(mutex_);
    if (!running_) {
        LOG(WARNING) << "Metric being skipped because reporter isn't running";
        return;
    }

    MetricsEvent event;
    event.time_ms = android::base::System::Get()->GetUnixTimeUs() / 1000;
    SetBaseFields(event.as_event);
    callback(event.as_event);
    event_queue_.push(std::move(event));
}

void MetricsReporter::Run() {
    VLOG(1) << "Metrics reporter Run enter";
    while (true) {
        auto has_event = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
            return !event_queue_.empty() || !running_;
        };
        absl::MutexLock lock(mutex_);
        mutex_.Await(absl::Condition(&has_event));

        if (event_queue_.empty() && !running_) {
            break;
        }

        auto event = std::move(event_queue_.front());
        event_queue_.pop();

        if (writer_) {
            writer_->Write(std::move(event));
        }
    }
    VLOG(1) << "Metrics reporter Run exit";
}

}  // namespace goldfish::metrics
