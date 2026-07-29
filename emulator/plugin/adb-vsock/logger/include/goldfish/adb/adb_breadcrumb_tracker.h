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
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/adb/adb_message_logger.h"
#include "goldfish/raw_circular_log.h"

namespace goldfish::adb {
/**
 * @class AdbBreadcrumbTracker
 * @brief Tracks ADB packets and logs them to a circular buffer for diagnostics.
 *
 * This class implements the AdbPacketCallback interface to intercept ADB packets
 * flowing between the host and guest. It translates these packets into
 * generalized Breadcrumb events.
 *
 * @section stateful_tracking Stateful Tracking
 * To provide better forensic context, this tracker maintains state about active
 * streams. It inspects 'OPEN' packets to identify the service associated with
 * a stream (e.g., "sync:", "shell:"). This allows linking subsequent 'WRTE'
 * and 'OKAY' packets to a specific service via the `flow_id` (which is formed
 * by combining arg0 and arg1).
 *
 * @section buffer_exhaustion_protection Buffer Exhaustion Protection
 * ADB traffic can be extremely high-volume (e.g., during file transfers).
 * To prevent ADB traffic from wrapping the circular buffer and evicting other
 * useful breadcrumbs, this tracker skips capturing data snippets for 'WRTE'
 * commands on 'sync:' streams. For these, it only captures the first 4 bytes
 * of the payload to identify the SYNC sub-command (e.g., 'STAT', 'SEND').
 *
 * @section leak_prevention Memory Leak Prevention
 * Since connections can be severed abruptly without a 'CLSE' packet, the state
 * tracking structures could potentially leak memory. To prevent this:
 * - The map of pending opens is bounded to 16 entries with FIFO eviction.
 * - The list of open streams is bounded to 31 entries (effectively 32 inline) with FIFO eviction.
 */
class AdbBreadcrumbTracker : public AdbPacketCallback {
  public:
    AdbBreadcrumbTracker(bool capture_snippets = true) : capture_snippets_(capture_snippets) {}
    ~AdbBreadcrumbTracker() = default;

    void OnPacket(const AMessage& message, const char* data, bool to_guest) override;
    void OnOutOfSync(const std::string& reason, bool to_guest) override;

    static goldfish::proto_data_store::RawCircularLog* GetLogForTesting();

  private:
    void HandleOpen(const AMessage& message, const char* data, bool to_guest)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_);
    void HandleOkay(const AMessage& message, bool to_guest)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_);
    void HandleClose(const AMessage& message, bool to_guest)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_);
    bool IsSyncStream(uint64_t flow_id) ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_);
    void ErasePendingOpen(uint64_t key) ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_);

    const bool capture_snippets_;
    absl::Mutex open_streams_mutex_;
    absl::InlinedVector<std::pair<uint64_t, bool>, 32> open_streams_
            ABSL_GUARDED_BY(open_streams_mutex_);
    absl::flat_hash_map<uint64_t, bool> pending_opens_ ABSL_GUARDED_BY(open_streams_mutex_);
    absl::InlinedVector<uint64_t, 16> pending_opens_order_ ABSL_GUARDED_BY(open_streams_mutex_);
};

}  // namespace goldfish::adb
