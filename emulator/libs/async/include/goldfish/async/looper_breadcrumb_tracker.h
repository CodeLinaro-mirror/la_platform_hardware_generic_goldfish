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

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"

#include "android/crashreport/looper_registrations.h"
#include "emulator/crashreport/include/android/crashreport/breadcrumb_proto.h"
#include "goldfish/raw_circular_log.h"

namespace goldfish::async {

using ::android::crashreport::BreadcrumbPhase;
using ::android::crashreport::DynamicBinaryAnnotation;
using ::android::crashreport::FlowId;
using ::android::crashreport::PayloadType;
using ::android::crashreport::StackAddress;

// Note: Crashpad has an internal limit (kValueMaxSize) of ~20KB per annotation.
// The circular log size must not exceed this limit.
constexpr size_t kCircularLogSize = 4096;

/**
 * @brief Configuration options for posting a task to the event loop.
 *
 * Provides control over task metadata, such as overriding the caller's program
 * counter or attaching a contextual identifier for diagnostics and tracing.
 */
struct PostOptions {
    StackAddress caller_pc =
            0;  ///< The PC of the caller posting this task (for minidump diagnostics).
    std::string_view context = {};  ///< A short diagnostic string describing the task source.
};

/**
 * @brief Manages thread-local diagnostic logging and annotations for an EventLoop.
 *
 * This class encapsulates the setup and management of looper name registrations
 * in the global directory, memory-mapped DynamicBinaryAnnotation block structures,
 * and the serialization of raw circular breadcrumb logs.
 */
class LooperBreadcrumbTracker {
  public:
    /**
     * @brief Constructs a new Looper Breadcrumb Tracker object.
     *
     * @param name The human-readable name of the event loop.
     */
    LooperBreadcrumbTracker(std::string name);
    ~LooperBreadcrumbTracker() = default;

    /**
     * @brief Logs the scheduling (posting) of a new event to the looper, allocating a flow ID.
     *
     * This method automatically allocates a globally unique flow identifier to track the event
     * flow from this post site through its eventual execution on the event loop.
     *
     * @param options Configuration options specifying task metadata for diagnostics.
     * @return The allocated globally unique flow identifier.
     */
    FlowId LogPost(const PostOptions& options);

    /**
     * @brief Logs the start of execution of a scheduled task on the looper thread.
     *
     * @param flow_id The unique identifier representing this task execution flow.
     */
    void LogExecute(FlowId flow_id);

  private:
    absl::Status LogEvent(FlowId flow_id, BreadcrumbPhase phase, PayloadType payload_type,
                          std::string_view payload);

    std::string name_;
    uint8_t loop_id_;
    DynamicBinaryAnnotation<kCircularLogSize> annotation_;
    std::unique_ptr<goldfish::proto_data_store::RawCircularLog> breadcrumb_log_;
};

}  // namespace goldfish::async
