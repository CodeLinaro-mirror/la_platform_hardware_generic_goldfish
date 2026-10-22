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
#include <deque>
#include <string>
#include <string_view>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "android/base/clock.h"
#include "contact_table.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/eventing/event_sources.h"
#include "input_service.pb.h"
#include "keyboard_sink.h"
#include "touch_device.h"

namespace goldfish::grpc::v2 {

/**
 * @brief The single owner of guest touch and keyboard state: an actor on the
 * QEMU event loop that applies the pointer, key, and text events of every input
 * session to the touch devices and the keyboard.
 *
 * Sessions post operations, which the loop applies in the order they were
 * posted, so keys and touches reach the guest in that order. Each contact
 * `(session, pointer)` holds a multi-touch slot on the device it was created
 * on, and stays bound to that device until released. Each key is down in the
 * guest while at least one session holds it.
 *
 * Everything that reaches the guest is reported, once and in the order the
 * loop applied it, as an `InputActivity` on `activity()`. The loop stamps and
 * sequences each activity into an outbox; a delivery thread fires them, so
 * observer work stays off the loop.
 *
 * ### Invariants:
 * - The `ContactTable` matches the contacts written to each active device.
 * - Each device receives at most one report per operation, written whole.
 * - A key is in `key_holders_`, with a non-empty set of open sessions, iff the
 *   last write of that key to the keyboard was a press.
 * - `outbox_` holds the undelivered activities in sequence order, ending just
 *   before `next_sequence_`.
 *
 * ### Thread Safety:
 * Public methods are thread-safe and post to the loop; all other state is
 * touched only on the loop, except the outbox, which is guarded by
 * `outbox_mutex_`. Lock order: `outbox_mutex_` is a leaf; nothing holds it
 * while writing to a sink or firing activity.
 *
 * ### Ownership:
 * `loop`, `devices`, `keyboard`, and `clock` must outlive the dispatcher. The
 * owner must stop posting, and let posted operations run, before destroying
 * it. Destruction delivers the queued activity, then joins the delivery thread.
 */
class InputDispatcher {
  public:
    struct SessionOptions {
        /** @brief Human sessions may use the slots reserved for human input. */
        bool human = false;
        /** @brief The `input_source_id` stamped on every activity of the session. */
        int32_t source_id = 0;
    };

    using ActivityEventSource =
            ::android::base::eventing::CallbackEventSource<::android::emulation::v2::InputActivity>;

    /** @brief Runs on the loop with OK or the first error of the operation. */
    using DispatchCallback = absl::AnyInvocable<void(absl::Status) &&>;

    /**
     * @brief Maps a key event to its Linux evdev code, or 0 if it has none.
     * Called only on the loop.
     */
    using KeyCodeResolver =
            absl::AnyInvocable<uint32_t(const ::android::emulation::v2::KeyEvent&) const>;

    /** @param clock Stamps `dispatched_at_nanos` from its realtime clock. */
    InputDispatcher(::goldfish::async::EventLoop& loop, TouchDevices& devices,
                    KeyboardSink& keyboard, KeyCodeResolver resolve_key_code,
                    ::android::base::IClock* clock = &::android::base::IClock::Get());
    ~InputDispatcher();

    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;

    /**
     * @brief Activity, with the source ID of the session and
     * `dispatched_at_nanos` read right after the write to the guest:
     * - Pointer: the event on the requested display, listing only the pointers
     *   written to a device; nothing if none was. A pointer-less release that
     *   released contacts is a `CANCEL` of their pointer IDs.
     * - Key: each applied `DOWN`, `UP`, and `PRESS`, even an `UP` the guest
     *   didn't get because another session holds the key. A stray `UP` applies
     *   nothing.
     * - Text: non-empty text.
     * - `CloseSession()`: a synthesized `CANCEL` per display the released
     *   contacts were requested on, and a synthesized `KEY_ACTION_UP` (as
     *   `evdev_code`) per released key.
     *
     * Rejected events report nothing.
     *
     * Callbacks run in order on the delivery thread. They must not block, and
     * must not call back into the dispatcher synchronously.
     */
    ActivityEventSource& activity() { return activity_events_; }

    /** @return A new session ID; IDs start at 1. */
    InputSessionId OpenSession(SessionOptions options);

    /**
     * @brief Releases all contacts of `session`, writing one report per active
     * device, releases the keys only it holds in ascending code order, and
     * forgets it.
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

    /**
     * @brief Applies `event` for `session` on the loop to the key the resolver
     * maps it to:
     * - `DOWN` makes the session a holder and always writes a press, so a
     *   client's autorepeat reaches the guest.
     * - `UP` from a holder removes it, and writes a release once no session
     *   holds the key. `UP` from a non-holder changes nothing.
     * - `PRESS`, `UNSPECIFIED`, and unknown actions are `DOWN` then `UP`, so a
     *   press while another session holds the key writes only a press.
     *
     * An unmapped key completes with `INVALID_ARGUMENT` and an unknown session
     * with `FAILED_PRECONDITION`; either way nothing is written.
     *
     * @param done As for `DispatchPointer()`.
     */
    void DispatchKey(InputSessionId session, ::android::emulation::v2::KeyEvent event,
                     DispatchCallback done = nullptr);

    /**
     * @brief Types `utf8` for `session` on the loop unless it is empty; text
     * holds no keys. An unknown session completes with `FAILED_PRECONDITION`.
     *
     * @param done As for `DispatchPointer()`.
     */
    void DispatchText(InputSessionId session, std::string utf8, DispatchCallback done = nullptr);

  private:
    struct SequencedActivity {
        uint64_t sequence;
        ::android::emulation::v2::InputActivity activity;
    };

    // Posts `apply` and completes `done` with its result.
    void PostOperation(absl::AnyInvocable<absl::Status() &&> apply, DispatchCallback done);
    absl::StatusOr<SessionOptions> FindSession(InputSessionId session) const;
    absl::Status ApplyPointer(InputSessionId session,
                              const ::android::emulation::v2::PointerEvent& event);
    absl::Status ApplyKey(InputSessionId session, const ::android::emulation::v2::KeyEvent& event);
    void HoldKey(InputSessionId session, uint32_t code);
    // @return Whether `session` held the key.
    bool ReleaseKey(InputSessionId session, uint32_t code);
    absl::Status ApplyText(InputSessionId session, std::string_view utf8);
    absl::Status CancelOnDisplay(InputSessionId session, int32_t source_id, int32_t display_id);
    void ReleaseSession(InputSessionId session);

    // Stamps `event` and queues it for delivery. Called on the loop right
    // after the write it reports.
    void Report(::android::emulation::v2::InputEvent event, bool synthesized = false)
            ABSL_LOCKS_EXCLUDED(outbox_mutex_);
    // The delivery thread: fires the outbox in order until stopped and empty.
    void DeliverActivity() ABSL_LOCKS_EXCLUDED(outbox_mutex_);
    bool HasActivityOrStopping() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(outbox_mutex_);

    ::goldfish::async::EventLoop& loop_;
    TouchDevices& devices_;
    KeyboardSink& keyboard_;
    const KeyCodeResolver resolve_key_code_;
    ::android::base::IClock* const clock_;
    std::atomic<InputSessionId> next_session_{1};

    // Loop-only state.
    ContactTable contacts_;
    absl::flat_hash_map<InputSessionId, SessionOptions> sessions_;
    // The sessions holding each pressed evdev code.
    absl::flat_hash_map<uint32_t, absl::flat_hash_set<InputSessionId>> key_holders_;

    ActivityEventSource activity_events_;
    absl::Mutex outbox_mutex_;
    std::deque<SequencedActivity> outbox_ ABSL_GUARDED_BY(outbox_mutex_);
    uint64_t next_sequence_ ABSL_GUARDED_BY(outbox_mutex_) = 0;
    bool stopping_ ABSL_GUARDED_BY(outbox_mutex_) = false;
    // Last, so it starts once the state it uses is built.
    std::thread delivery_thread_;
};

}  // namespace goldfish::grpc::v2
