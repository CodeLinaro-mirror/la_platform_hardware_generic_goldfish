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
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "aemu/base/Debug.h"
#include "android/base/clock.h"
#include "goldfish/async/event_loop.h"

namespace android::crashreport {

namespace {
class LoopWatcher {
  public:
    LoopWatcher(std::string loop_name, ::goldfish::async::EventLoop& event_loop,
                absl::Duration hang_timeout, absl::Duration hang_check_timeout,
                const android::base::IClock* clock)
            : mLoopName(std::move(loop_name))
            , mTimeout(hang_timeout)
            , mhangCheckTimeout(hang_check_timeout)
            , mClock(clock)
            , mTimer(event_loop.CreateTimer([this]() { taskComplete(); })) {}

    ~LoopWatcher() { cancelHangCheck(); }

    LoopWatcher(LoopWatcher&&) = delete;
    LoopWatcher& operator=(LoopWatcher&&) = delete;
    LoopWatcher(const LoopWatcher&) = delete;
    LoopWatcher& operator=(const LoopWatcher&) = delete;

    void startHangCheck() {
        absl::MutexLock l(&mMutex);
        scheduleHangCheckLocked();
    }

    void cancelHangCheck() {
        absl::MutexLock l(&mMutex);

        if (mTimer) {
            mTimer->Cancel();
            mTimer.reset();
        }
        mIsTaskRunning = false;
    }

    void process(const HangDetector::HangCallback& hangCallback) {
        absl::ReleasableMutexLock l(&mMutex);

        const absl::Time now = mClock->Now(base::ClockType::kRealtime);
        if (mIsTaskRunning) {
            // Heuristic: If the looper watcher itself took much longer than
            // mTimeout to fire again, it's possible there was a system-wide
            // sleep. In this case, don't count that as hanging.
            // Note that we are using std::chrono to make sure all the durations
            // are scaled appropriately.
            const absl::Time timeSystemLiveAndHanging = mLastCheckTime + mTimeout;
            const absl::Time timeSystemSleepedPastHangTimeout = mLastCheckTime + 2 * mTimeout;
            if (now > timeSystemLiveAndHanging && now < timeSystemSleepedPastHangTimeout) {
                absl::Duration timePassed = now - mLastCheckTime;
                const auto message = absl::StrCat("detected a hanging thread '", mLoopName,
                                                  "'. No response for ", timePassed);
                ++mHangCount;

                LOG(ERROR) << message
                           << (android::base::IsDebuggerAttached() ? ", ignored (debugger attached)"
                                                                   : "");
                if (mHangCount >= kMaxHangCount && hangCallback &&
                    !android::base::IsDebuggerAttached()) {
                    l.Release();
                    hangCallback(message);
                    return;
                } else {
                    // Start another hang check in case something happened to
                    // this previous event.
                    scheduleHangCheckLocked();
                }
            }
        } else if (now > mLastCheckTime + mhangCheckTimeout) {
            mHangCount = 0;
            scheduleHangCheckLocked();
        }
    }

  private:
    void scheduleHangCheckLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mMutex) {
        mIsTaskRunning = true;
        mLastCheckTime = mClock->Now(base::ClockType::kRealtime);
        // 0 means run as soon as possible.
        mTimer->Schedule(std::chrono::milliseconds(0));
    }

    void taskComplete() {
        absl::MutexLock l(&mMutex);
        mIsTaskRunning = false;
    }

    const std::string mLoopName;
    const absl::Duration mTimeout;
    const absl::Duration mhangCheckTimeout;
    const android::base::IClock* const mClock;

    absl::Mutex mMutex;
    std::shared_ptr<::goldfish::async::EventLoop::Timer> mTimer ABSL_GUARDED_BY(mMutex);
    bool mIsTaskRunning ABSL_GUARDED_BY(mMutex) = false;
    absl::Time mLastCheckTime ABSL_GUARDED_BY(mMutex);
    int mHangCount ABSL_GUARDED_BY(mMutex) = 0;

    static constexpr int kMaxHangCount = 2;
};

class HangDetectorImpl : public HangDetector {
  public:
    HangDetectorImpl(HangCallback hangCallback, Timing timing,
                     std::unique_ptr<android::base::IClock> clock)
            : mHangCallback(std::move(hangCallback))
            , mTiming(std::move(timing))
            , mClock(std::move(clock))
            , mWorkerThread([this]() { workerThread(); }) {}

    ~HangDetectorImpl() override { stop(); }

    void addWatchedLooper(std::string loop_name, ::goldfish::async::EventLoop& event_loop,
                          absl::Duration task_timeout) override {
        absl::MutexLock l(&mMutex);
        if (mStopping) {
            return;
        }
        mLoopWatchers.emplace_back(
                std::make_unique<LoopWatcher>(std::move(loop_name), event_loop, task_timeout,
                                              mTiming.hangCheckTimeout, mClock.get()));
        mLoopWatchers.back()->startHangCheck();
    }

    void addPredicateCheck(HangPredicate predicate, std::string msg) override {
        absl::MutexLock l(&mMutex);
        mPredicates.emplace_back(std::make_pair(std::move(predicate), std::move(msg)));
    }

    void addPredicateCheck(StatefulHangdetector* detector, std::string msg) override {
        {
            absl::MutexLock l(&mMutex);
            mRegistered.push_back(std::unique_ptr<StatefulHangdetector>(detector));
        }
        HangPredicate pred = [detector] { return detector->check(); };
        addPredicateCheck([detector] { return detector->check(); }, std::move(msg));
    }

    void stop() override {
        {
            absl::MutexLock l(&mMutex);
            if (mStopping) {
                return;
            }
            mStopping = true;
            for (auto& lw : mLoopWatchers) {
                lw->cancelHangCheck();
            }
        }

        assert(mWorkerThread.joinable());
        mWorkerThread.join();
    }

  private:
    void workerThread() {
        auto await = [this] ABSL_EXCLUSIVE_LOCKS_REQUIRED(mMutex) { return mStopping; };
        absl::MutexLock l(&mMutex);
        for (;;) {
            if (mMutex.AwaitWithTimeout(absl::Condition(&await),
                                        mTiming.hangLoopIterationTimeout)) {
                if (mStopping) {
                    break;
                }
            }
            for (auto&& lw : mLoopWatchers) {
                lw->process(mHangCallback);
            }

            // Check to see if any of the predicates evaluate to true.
            for (const auto& predicate : mPredicates) {
                if (predicate.first()) {
                    const auto message = absl::StrFormat("Failed hang detection predicate: '%s'",
                                                         predicate.second);

                    LOG(ERROR) << message
                               << (android::base::IsDebuggerAttached()
                                           ? ", ignored (debugger attached)"
                                           : "");

                    if (mHangCallback && !android::base::IsDebuggerAttached()) {
                        mHangCallback(message);
                    }
                }
            }
        }
    }

    const HangCallback mHangCallback;
    const Timing mTiming;
    const std::unique_ptr<android::base::IClock> mClock;

    std::vector<std::unique_ptr<LoopWatcher>> mLoopWatchers ABSL_GUARDED_BY(mMutex);
    std::vector<std::pair<HangPredicate, std::string>> mPredicates ABSL_GUARDED_BY(mMutex);
    std::vector<std::unique_ptr<StatefulHangdetector>> mRegistered ABSL_GUARDED_BY(mMutex);

    absl::Mutex mMutex;
    bool mStopping ABSL_GUARDED_BY(mMutex) = false;

    // A separate worker thread so it's not affected if anything hangs.
    std::thread mWorkerThread;
};

}  // namespace

std::unique_ptr<HangDetector> HangDetector::create(HangCallback hangCallback, Timing timing,
                                                   std::unique_ptr<android::base::IClock> clock) {
    return std::make_unique<HangDetectorImpl>(std::move(hangCallback), std::move(timing),
                                              std::move(clock));
}

}  // namespace android::crashreport
