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

#include "android/crashreport/hang_detector.h"

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "android/base/clock.h"
#include "android/crashreport/debug.h"
#include "android/goldfish/vm_interface.h"
#include "goldfish/async/event_loop.h"

namespace android::crashreport {

namespace {
class LoopWatcher {
  public:
    LoopWatcher(std::string loop_name, ::goldfish::async::EventLoop& event_loop,
                absl::Duration hang_timeout, absl::Duration hang_check_timeout,
                const android::base::IClock* clock)
            : loop_name_(std::move(loop_name))
            , timeout_(hang_timeout)
            , hang_check_timeout_(hang_check_timeout)
            , clock_(clock)
            , event_loop_(event_loop)
            , timer_(event_loop.CreateTimer([this]() {
                TaskComplete();
                return true;
            })) {}

    ~LoopWatcher() { CancelHangCheck(); }

    LoopWatcher(LoopWatcher&&) = delete;
    LoopWatcher& operator=(LoopWatcher&&) = delete;
    LoopWatcher(const LoopWatcher&) = delete;
    LoopWatcher& operator=(const LoopWatcher&) = delete;

    bool IsOnTheLoop(::goldfish::async::EventLoop& rhs) const { return &event_loop_ == &rhs; }

    void StartHangCheck() {
        const absl::MutexLock l(&mutex_);
        ScheduleHangCheckLocked();
    }

    void CancelHangCheck() {
        std::shared_ptr<::goldfish::async::EventLoop::Timer> timer;
        {
            const absl::MutexLock l(&mutex_);
            timer = std::move(timer_);
            is_task_running_ = false;
        }

        if (timer) {
            timer->Cancel();
        }
    }

    void Process(const HangDetector::HangCallback& hang_callback) {
        absl::ReleasableMutexLock l(&mutex_);

        const absl::Time now = clock_->Now(base::ClockType::kRealtime);
        if (is_task_running_) {
            // Heuristic: If the looper watcher itself took much longer than
            // mTimeout to fire again, it's possible there was a system-wide
            // sleep. In this case, don't count that as hanging.
            // Note that we are using std::chrono to make sure all the durations
            // are scaled appropriately.
            const absl::Time time_system_live_and_hanging = last_check_time_ + timeout_;
            const absl::Time time_system_sleeped_past_hang_timeout =
                    last_check_time_ + 2 * timeout_;
            if (now > time_system_live_and_hanging && now < time_system_sleeped_past_hang_timeout) {
                const absl::Duration time_passed = now - last_check_time_;
                const auto message = absl::StrCat("detected a hanging thread '", loop_name_,
                                                  "'. No response for ", time_passed);
                ++hang_count_;

                LOG(ERROR) << message
                           << (android::base::IsDebuggerAttached() ||
                                               !android::goldfish::VmOperations::qemuVmOperations()
                                                        ->isRunning()
                                       ? ", ignored (debugger attached or vm stopped)"
                                       : "");
                if (hang_count_ >= kMaxHangCount && hang_callback &&
                    !android::base::IsDebuggerAttached() &&
                    android::goldfish::VmOperations::qemuVmOperations()->isRunning()) {
                    l.Release();
                    hang_callback(message);
                    return;
                }
                // Start another hang check in case something happened to this previous event.
                ScheduleHangCheckLocked();
            }
        } else if (now > last_check_time_ + hang_check_timeout_) {
            hang_count_ = 0;
            ScheduleHangCheckLocked();
        }
    }

  private:
    void ScheduleHangCheckLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
        is_task_running_ = true;
        last_check_time_ = clock_->Now(base::ClockType::kRealtime);
        // 0 means run as soon as possible.
        if (timer_) {
            timer_->Schedule(std::chrono::milliseconds(0));
        }
    }

    void TaskComplete() {
        const absl::MutexLock l(&mutex_);
        is_task_running_ = false;
    }

    const std::string loop_name_;
    const absl::Duration timeout_;
    const absl::Duration hang_check_timeout_;
    const android::base::IClock* const clock_;

    absl::Mutex mutex_;
    ::goldfish::async::EventLoop& event_loop_;
    std::shared_ptr<::goldfish::async::EventLoop::Timer> timer_ ABSL_GUARDED_BY(mutex_);
    bool is_task_running_ ABSL_GUARDED_BY(mutex_) = false;
    absl::Time last_check_time_ ABSL_GUARDED_BY(mutex_);
    int hang_count_ ABSL_GUARDED_BY(mutex_) = 0;

    static constexpr int kMaxHangCount = 2;
};

class HangDetectorImpl : public HangDetector {
  public:
    HangDetectorImpl(HangCallback hang_callback, Timing timing,
                     std::unique_ptr<android::base::IClock> clock)
            : hang_callback_(std::move(hang_callback))
            , timing_(std::move(timing))
            , clock_(std::move(clock))
            , worker_thread_([this]() { WorkerThread(); }) {}

    ~HangDetectorImpl() override { Stop(); }

    void AddWatchedLooper(std::string loop_name, ::goldfish::async::EventLoop& event_loop,
                          absl::Duration task_timeout) override {
        const absl::MutexLock l(&mutex_);
        if (stopping_) {
            return;
        }
        loop_watchers_.emplace_back(
                std::make_shared<LoopWatcher>(std::move(loop_name), event_loop, task_timeout,
                                              timing_.hang_check_timeout, clock_.get()));
        loop_watchers_.back()->StartHangCheck();
    }

    void RemoveWatchedLooper(::goldfish::async::EventLoop& event_loop) override {
        std::vector<std::shared_ptr<LoopWatcher>> to_remove;
        {
            const absl::MutexLock l(&mutex_);
            if (stopping_) {
                return;
            }

            for (auto it = loop_watchers_.begin(); it != loop_watchers_.end();) {
                if ((*it)->IsOnTheLoop(event_loop)) {
                    to_remove.push_back(std::move(*it));
                    it = loop_watchers_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (auto& lw : to_remove) {
            lw->CancelHangCheck();
        }
    }

    void AddPredicateCheck(HangPredicate predicate, std::string msg) override {
        const absl::MutexLock l(&mutex_);
        predicates_.emplace_back(std::move(predicate), std::move(msg));
    }

    void AddPredicateCheck(StatefulHangdetector* detector, std::string msg) override {
        {
            const absl::MutexLock l(&mutex_);
            registered_.push_back(std::unique_ptr<StatefulHangdetector>(detector));
        }
        const HangPredicate pred = [detector] { return detector->Check(); };
        AddPredicateCheck([detector] { return detector->Check(); }, std::move(msg));
    }

    void Stop() override {
        std::vector<std::shared_ptr<LoopWatcher>> loop_watchers;
        {
            const absl::MutexLock l(&mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            loop_watchers = std::move(loop_watchers_);
        }

        for (auto& lw : loop_watchers) {
            lw->CancelHangCheck();
        }

        DCHECK(worker_thread_.joinable());
        worker_thread_.join();
    }

  private:
    void WorkerThread() {
        auto await = [this] ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return stopping_; };
        for (;;) {
            std::vector<std::shared_ptr<LoopWatcher>> watchers;
            std::vector<std::pair<HangPredicate, std::string>> predicates;
            {
                const absl::MutexLock l(&mutex_);
                if (mutex_.AwaitWithTimeout(absl::Condition(&await),
                                            timing_.hang_loop_iteration_timeout)) {
                    if (stopping_) {
                        break;
                    }
                }
                watchers = loop_watchers_;
                predicates = predicates_;
            }

            for (auto&& lw : watchers) {
                lw->Process(hang_callback_);
            }

            // Check to see if any of the predicates evaluate to true.
            for (const auto& predicate : predicates) {
                if (predicate.first()) {
                    const auto message = absl::StrFormat("Failed hang detection predicate: '%s'",
                                                         predicate.second);

                    LOG(ERROR)
                            << message
                            << (android::base::IsDebuggerAttached() ||
                                                !android::goldfish::VmOperations::qemuVmOperations()
                                                         ->isRunning()
                                        ? ", ignored (debugger attached or vm stopped)"
                                        : "");

                    if (hang_callback_ && !android::base::IsDebuggerAttached() &&
                        android::goldfish::VmOperations::qemuVmOperations()->isRunning()) {
                        hang_callback_(message);
                    }
                }
            }
        }
    }

    const HangCallback hang_callback_;
    const Timing timing_;
    const std::unique_ptr<android::base::IClock> clock_;

    std::vector<std::shared_ptr<LoopWatcher>> loop_watchers_ ABSL_GUARDED_BY(mutex_);
    std::vector<std::pair<HangPredicate, std::string>> predicates_ ABSL_GUARDED_BY(mutex_);
    std::vector<std::unique_ptr<StatefulHangdetector>> registered_ ABSL_GUARDED_BY(mutex_);

    absl::Mutex mutex_;
    bool stopping_ ABSL_GUARDED_BY(mutex_) = false;

    // A separate worker thread so it's not affected if anything hangs.
    std::thread worker_thread_;
};

}  // namespace

std::unique_ptr<HangDetector> HangDetector::Create(HangCallback hang_callback, Timing timing,
                                                   std::unique_ptr<android::base::IClock> clock) {
    return std::make_unique<HangDetectorImpl>(std::move(hang_callback), std::move(timing),
                                              std::move(clock));
}

}  // namespace android::crashreport
