// Copyright 2017 The Android Open Source Project
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

#include <functional>
#include <memory>
#include <string_view>

#include "absl/time/time.h"

#include "android/base/clock.h"
#include "goldfish/async/event_loop.h"

namespace android::crashreport {

// Use this interface if your hangdetector needs to
// keep track of state.
class StatefulHangdetector {
  public:
    virtual ~StatefulHangdetector() {};
    virtual bool check() = 0;
};

/**
 * HangDetector - a class that monitors a set of Loopers and checks if any of
 * those is hanging. It calls a user-supplied callback in that case.
 *
 * HangDetector uses EventLoop::createTimer() to get a timer object for each event
 * loop it watches. Separate thread wakes every couple of seconds to check if
 * it needs to schedule a new task on a looper, or, if a task was scheduled for
 * a while and didn't finish in time, to call the |hangCallback|.
 *
 * Note: Be careful with the timing.hangLoopIterationTimeout. Setting it too
 * aggressively can prevent the hang detector from functioning properly.
 */
class HangDetector {
  public:
    struct Timing {
        // Timeout between worker thread's loop iterations.
        const absl::Duration hangLoopIterationTimeout;
        // Timeout between hang checks.
        const absl::Duration hangCheckTimeout;
    };

    static constexpr Timing defaultTiming() {
        return {.hangLoopIterationTimeout = absl::Seconds(5),
                .hangCheckTimeout = absl::Seconds(15)};
    }

    using HangCallback = std::function<void(std::string_view message)>;
    using HangPredicate = std::function<bool()>;

    HangDetector() = default;
    virtual ~HangDetector() = default;
    HangDetector(const HangDetector&) = delete;
    HangDetector& operator=(const HangDetector&) = delete;
    HangDetector(HangDetector&&) = delete;
    HangDetector& operator=(HangDetector&&) = delete;

    virtual void addWatchedLooper(std::string loop_name, ::goldfish::async::EventLoop& event_loop,
                                  absl::Duration task_timeout) = 0;

    // We implicitly assume:
    //    predicate() -> []predicate() (if a predicate becomes true, it will
    //    always return true, we only need to infer a system hangs once)
    virtual void addPredicateCheck(HangPredicate predicate, std::string msg) = 0;

    // Registers a stateful hangdetector. This class will take ownership of the
    // object
    virtual void addPredicateCheck(StatefulHangdetector* detector, std::string msg) = 0;

    virtual void stop() = 0;

    static std::unique_ptr<HangDetector> create(HangCallback hangCallback, Timing timing,
                                                std::unique_ptr<android::base::IClock> clock);
};

}  // namespace android::crashreport
