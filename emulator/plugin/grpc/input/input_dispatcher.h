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

#include <atomic>
#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include "contact_table.h"
#include "goldfish/async/event_loop.h"
#include "input_service.pb.h"
#include "touch_device.h"

namespace goldfish::grpc::v2 {

/**
 * @brief The single owner of guest touch state: an actor on the QEMU event loop
 * that applies the pointer events of every input session to the touch devices.
 *
 * Sessions post operations, which the loop applies in the order they were
 * posted. Each contact `(session, pointer)` holds a multi-touch slot on the
 * device it was created on, and stays bound to that device until released.
 *
 * ### Invariants:
 * - The `ContactTable` matches the contacts written to each active device.
 * - Each device receives at most one report per operation, written whole.
 *
 * ### Thread Safety:
 * Public methods are thread-safe and post to the loop; all other state is
 * touched only on the loop.
 *
 * ### Ownership:
 * `loop` and `devices` must outlive the dispatcher. The owner must stop
 * posting, and let posted operations run, before destroying it.
 */
class InputDispatcher {
  public:
    struct SessionOptions {
        /** @brief Human sessions may use the slots reserved for human input. */
        bool human = false;
    };

    /** @brief Runs on the loop with OK or the first error of the operation. */
    using DispatchCallback = absl::AnyInvocable<void(absl::Status) &&>;

    InputDispatcher(::goldfish::async::EventLoop& loop, TouchDevices& devices);

    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;

    /** @return A new session ID; IDs start at 1. */
    InputSessionId OpenSession(SessionOptions options);

    /**
     * @brief Releases all contacts of `session`, writing one report per active
     * device, and forgets it.
     */
    void CloseSession(InputSessionId session);

    /**
     * @brief Applies `event` for `session` on the loop.
     *
     * - `RELATIVE_MOVE` and `SCROLL` change nothing and write nothing.
     * - `CANCEL` or `HOVER_EXIT` without pointers releases the session's
     *   contacts on the device `display_id` resolves to.
     * - A listed pointer the session holds moves on its bound device, or is
     *   lifted if `IsContactReleased()`; `DOWN` for a held pointer is a move.
     * - An unheld pointer becomes a new contact on the device `display_id`
     *   resolves to if the action is `DOWN`, `HOVER_ENTER`, or `HOVER_MOVE`,
     *   and is ignored otherwise. If no slot is free it is skipped and the
     *   event completes with `RESOURCE_EXHAUSTED`; other pointers still apply.
     *
     * If a target device is missing or inactive the event completes with
     * `UNAVAILABLE`, and if `display_id` doesn't resolve with that error;
     * either way nothing is written or changed. An unknown session completes
     * with `FAILED_PRECONDITION`.
     *
     * @param done Runs on the loop with the result, or on the calling thread
     *     with the posting error if the loop rejects the operation.
     */
    void DispatchPointer(InputSessionId session, ::android::emulation::v2::PointerEvent event,
                         DispatchCallback done = nullptr);

  private:
    absl::Status ApplyPointer(InputSessionId session,
                              const ::android::emulation::v2::PointerEvent& event);
    absl::Status CancelOnDisplay(InputSessionId session, int32_t display_id);
    void ReleaseSession(InputSessionId session);

    ::goldfish::async::EventLoop& loop_;
    TouchDevices& devices_;
    std::atomic<InputSessionId> next_session_{1};

    // Loop-only state.
    ContactTable contacts_;
    absl::flat_hash_map<InputSessionId, SessionOptions> sessions_;
};

}  // namespace goldfish::grpc::v2
