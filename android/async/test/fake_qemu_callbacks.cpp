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

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "goldfish/async/qemu_event_loop.h"
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

// --- I/O Loop State ---
struct FdHandler {
    IOHandler* read_cb;
    IOHandler* write_cb;
    void* opaque;
};

std::map<int, FdHandler> sFdHandlers;
std::atomic<bool> sIoLoopRunning(false);
std::thread sIoLoopThread;
// A self-pipe used to wake up the poll() call in the I/O loop thread when
// the set of monitored file descriptors changes.
int sPipeFd[2] = {-1, -1};

// The io_loop is the heart of the fake QEMU I/O handling. It runs in a
// dedicated thread and mimics the behavior of QEMU's main event loop for
// file descriptor I/O.
//
// The loop does the following:
// 1. Collects all registered file descriptors and their requested events
//    (read/write) from the global `sFdHandlers` map.
// 2. Uses `poll()` to wait for I/O events on these descriptors.
// 3. The `poll()` call blocks indefinitely until an event occurs. To handle
//    changes in the set of monitored file descriptors (e.g., a new socket is
//    added or removed), we need a way to wake up the `poll()` call. This is
//    where `sPipeFd` comes in.
// 4. `sPipeFd` is a self-pipe. The read end of the pipe is always included
//    in the `poll()` set. When another thread modifies `sFdHandlers`, it
//    writes a single byte to the write end of the pipe. This wakes up
//    `poll()`, causing the loop to re-read the `sFdHandlers` and update the
//    set of polled file descriptors.
// 5. When `poll()` returns, the loop iterates through the descriptors with
//    events and invokes their corresponding read/write callbacks.
void io_loop() {
    goldfish::async::initializeQemuEventLoop();
    while (sIoLoopRunning.load()) {
        std::vector<struct pollfd> pollfds;
        std::map<int, FdHandler> current_handlers;
        {
            std::lock_guard<std::recursive_mutex> lock(sMutex);
            current_handlers = sFdHandlers;
        }

        for (const auto& [fd, handler] : current_handlers) {
            struct pollfd pfd = {0};
            pfd.fd = fd;
            if (handler.read_cb) {
                pfd.events |= POLLIN;
            }
            if (handler.write_cb) {
                pfd.events |= POLLOUT;
            }
            pollfds.push_back(pfd);
        }

        // Add the pipe fd to be able to wake up the poll
        struct pollfd pfd = {0};
        pfd.fd = sPipeFd[0];
        pfd.events = POLLIN;
        pollfds.push_back(pfd);

        int ret = poll(pollfds.data(), pollfds.size(), -1);
        if (ret <= 0 || !sIoLoopRunning.load()) {
            continue;
        }

        for (const auto& p : pollfds) {
            if (p.fd == sPipeFd[0]) {
                if (p.revents & POLLIN) {
                    char buf[1];
                    read(sPipeFd[0], buf, 1);
                }
                continue;
            }

            if (p.revents & (POLLIN | POLLHUP | POLLERR)) {
                FdHandler handler;
                {
                    std::lock_guard<std::recursive_mutex> lock(sMutex);
                    handler = sFdHandlers[p.fd];
                }
                if (handler.read_cb) {
                    handler.read_cb(handler.opaque);
                }
            }
            if (p.revents & (POLLOUT | POLLHUP | POLLERR)) {
                FdHandler handler;
                {
                    std::lock_guard<std::recursive_mutex> lock(sMutex);
                    handler = sFdHandlers[p.fd];
                }
                if (handler.write_cb) {
                    handler.write_cb(handler.opaque);
                }
            }
        }
    }
}

}  // namespace

extern "C" {

// --- Public Test Control Functions ---

void fake_qemu_start_io_loop() {
    if (sIoLoopRunning.load()) return;
    pipe(sPipeFd);
    sIoLoopRunning = true;
    sIoLoopThread = std::thread(io_loop);
}

void fake_qemu_stop_io_loop() {
    if (!sIoLoopRunning.load()) return;
    sIoLoopRunning = false;
    // Wake up the poll() call
    char c = 0;
    write(sPipeFd[1], &c, 1);
    if (sIoLoopThread.joinable()) {
        sIoLoopThread.join();
    }
    close(sPipeFd[0]);
    close(sPipeFd[1]);
    sPipeFd[0] = -1;
    sPipeFd[1] = -1;
}

void fake_qemu_advance_ms(int64_t ms) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    int64_t deadline = sFakeClockMs + ms;
    while (sFakeClockMs < deadline) {
        sFakeClockMs++;

        for (auto* timer : sTimers) {
            if (timer->scale && timer->expire_time <= sFakeClockMs) {
                timer->scale = 0;
                timer->cb(timer->opaque);
            }
        }

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

void fake_qemu_reset() {
    fake_qemu_stop_io_loop();
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
    sFdHandlers.clear();
}

// --- Fake QEMU API Implementations ---

void qemu_set_fd_handler(int fd, IOHandler* fd_read, IOHandler* fd_write, void* opaque) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!fd_read && !fd_write) {
        sFdHandlers.erase(fd);
    } else {
        sFdHandlers[fd] = {fd_read, fd_write, opaque};
    }
    // Wake up poll() to notice the change
    if (sIoLoopRunning.load()) {
        char c = 0;
        write(sPipeFd[1], &c, 1);
    }
}

int64_t qemu_clock_get_ns(QEMUClockType type) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    return sFakeClockMs ? sFakeClockMs * 1000000 : 0;
}

void timer_init_full(QEMUTimer* ts, QEMUTimerListGroup* timer_list_group, QEMUClockType type,
                     int scale, int attributes, QEMUTimerCB* cb, void* opaque) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = 0;
    ts->attributes = 0;
    sTimers.push_back(ts);
}

void timer_mod(QEMUTimer* ts, int64_t expire_time) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!ts) return;
    ts->expire_time = expire_time;
    ts->scale = 1;
}

void timer_del(QEMUTimer* ts) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!ts) return;
    sTimers.remove(ts);
}

QEMUBH* qemu_bh_new_full(QEMUBHFunc* cb, void* opaque, const char* name,
                         MemReentrancyGuard* reentrancy_guard) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    auto* bh = new QEMUBH{cb, opaque, false, false};
    sBottomHalves.push_back(bh);
    return bh;
}

void qemu_bh_schedule(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh || bh->scheduled || bh->deleted) {
        return;
    }
    bh->scheduled = true;
    sScheduledBHs.push_back(bh);
}

void qemu_bh_cancel(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh) return;
    bh->scheduled = false;
    auto it = std::remove(sScheduledBHs.begin(), sScheduledBHs.end(), bh);
    sScheduledBHs.erase(it, sScheduledBHs.end());
}

void qemu_bh_delete(QEMUBH* bh) {
    std::lock_guard<std::recursive_mutex> lock(sMutex);
    if (!bh) return;
    bh->deleted = true;
    qemu_bh_cancel(bh);
    sBottomHalves.remove(bh);
    delete bh;
}

}  // extern "C"