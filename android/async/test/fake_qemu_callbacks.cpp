// Copyright (C) 2025 The Android Open Source Project
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

#include <algorithm>
#include <list>
#include <mutex>
#include <vector>

extern "C" {

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"

// IWYU pragma: end_keep
// clang-format on
}

// This file provides a fake implementation of the QEMU main loop functions
// that are used by QemuEventLoop. This allows for unit testing of the
// event loop logic without running inside the full QEMU environment.
//
// The implementation is based on a manually advanced clock (`sFakeClockMs`)
// and lists of pending timers and bottom-halves (BHs). The test code is
// responsible for calling `fake_qemu_advance_ms()` to process events.
//
// This fake is not thread-safe for concurrent calls to `fake_qemu_advance_ms`,
// but it is safe for other threads to schedule timers/BHs while the clock is
// being advanced.

// A fake QEMU Bottom-Half (BH) structure. A BH is a callback that is
// scheduled to run on the next iteration of the event loop.
struct QEMUBH {
    QEMUBHFunc* cb;
    void* opaque;
    bool deleted;    // True if the BH has been deleted and should be ignored.
    bool scheduled;  // True if the BH is currently scheduled to run.
};

namespace {

// A recursive mutex to protect all access to the global fake QEMU state.
// This allows callbacks to schedule new timers/BHs safely.
std::recursive_mutex sMutex;

// The master fake clock, in milliseconds. Tests manually advance this clock.
int64_t sFakeClockMs = 0;

// A list of all active QEMU timers.
std::list<QEMUTimer*> sTimers;
// A list of all created BHs, used for tracking and cleanup.
std::list<QEMUBH*> sBottomHalves;
// A vector of BHs that are scheduled to run in the current event loop tick.
std::vector<QEMUBH*> sScheduledBHs;

}  // namespace

extern "C" {

// --- Public Test Control Functions ---

// Advances the fake clock by `ms` milliseconds and processes any events that
// occurred during that time. This is the main function tests use to drive the
// event loop.
void fake_qemu_advance_ms(int64_t ms) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    int64_t deadline = sFakeClockMs + ms;
    while (sFakeClockMs < deadline) {
        sFakeClockMs++;

        // Fire any timers that have expired.
        // We iterate through all timers and check if their expiration time has
        // been reached.
        // NOTE: The `scale` field is abused here to mean "active". A timer is
        // considered active if `scale` is 1. When a timer fires, we set `scale`
        // to 0, effectively making it a one-shot timer from the perspective of
        // this fake implementation. The QemuTimer implementation is responsible
        // for re-arming repeating timers by calling `timer_mod` again.
        for (auto* timer : sTimers) {
            if (timer->scale && timer->expire_time <= sFakeClockMs) {
                timer->scale = 0;  // Deactivate after firing.
                timer->cb(timer->opaque);
            }
        }

        // Run scheduled bottom-halves (BHs).
        // We copy the scheduled BHs list and clear the original so that
        // callbacks can schedule new BHs without affecting the current iteration.
        if (!sScheduledBHs.empty()) {
            auto scheduled = sScheduledBHs;
            sScheduledBHs.clear();
            for (auto* bh : scheduled) {
                if (!bh->deleted) {
                    bh->scheduled = false;
                    bh->cb(bh->opaque);
                }
            }
        }
    }
}

// Resets all fake QEMU state to its initial values. This should be called
// between tests to ensure isolation.
void fake_qemu_reset() {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    sFakeClockMs = 0;

    for (auto* timer : sTimers) {
        delete timer;
    }
    sTimers.clear();

    for (auto* bh : sBottomHalves) {
        delete bh;
    }
    sBottomHalves.clear();
    sScheduledBHs.clear();
}

// --- Fake QEMU API Implementations ---

// Returns the current fake clock time in nanoseconds.
int64_t qemu_clock_get_ns(QEMUClockType type) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    // The fake clock is in milliseconds, so we convert to nanoseconds.
    return sFakeClockMs ? sFakeClockMs * 1000000 : 0;
}

// Initializes a new timer and adds it to the global list of timers.
void timer_init_full(QEMUTimer* ts, QEMUTimerListGroup* timer_list_group, QEMUClockType type,
                     int scale, int attributes, QEMUTimerCB* cb, void* opaque) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = 0;  // Initially inactive.
    ts->attributes = 0;
    sTimers.push_back(ts);
}

// Modifies a timer's expiration time and activates it.
void timer_mod(QEMUTimer* ts, int64_t expire_time) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!ts) return;
    ts->expire_time = expire_time;
    ts->scale = 1;  // Mark as active.
}

// Deletes a timer, removing it from the active list.
void timer_del(QEMUTimer* ts) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!ts) return;
    sTimers.remove(ts);
}

// Creates a new BH and adds it to the tracking list.
QEMUBH* qemu_bh_new_full(QEMUBHFunc* cb, void* opaque, const char* name,
                         MemReentrancyGuard* reentrancy_guard) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    auto* bh = new QEMUBH{cb, opaque, false, false};
    sBottomHalves.push_back(bh);
    return bh;
}

// Schedules a BH to be run on the next loop iteration.
void qemu_bh_schedule(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh || bh->scheduled || bh->deleted) {
        return;
    }
    bh->scheduled = true;
    sScheduledBHs.push_back(bh);
}

// Cancels a scheduled BH, preventing it from running.
void qemu_bh_cancel(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh) return;
    bh->scheduled = false;
    auto it = std::remove(sScheduledBHs.begin(), sScheduledBHs.end(), bh);
    sScheduledBHs.erase(it, sScheduledBHs.end());
}

// Deletes a BH entirely, cancelling and removing it from all tracking lists.
void qemu_bh_delete(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh) return;
    bh->deleted = true;
    qemu_bh_cancel(bh);
    sBottomHalves.remove(bh);
    delete bh;
}

}  // extern "C"
