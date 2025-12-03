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
#include "goldfish/async/testing/test_event_loop.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "absl/status/status.h"

namespace goldfish::async::testing {

// The concrete implementation class, hidden entirely within this .cpp file.
class TestEventLoopImpl : public TestEventLoop {
    class TestTimer;

  public:
    TestEventLoopImpl();
    ~TestEventLoopImpl() override;

    // EventLoop Interface
    std::future<absl::Status> shutdown() override;
    bool isOnLoopThread() const override;
    absl::Status postImmediately(Task task) override;
    absl::Status postDelayed(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> createTimer(Task task) override;

    // TestEventLoop Interface
    void runAll() override;
    bool runOne() override;
    size_t runMany(size_t count) override;
    void advanceClock(std::chrono::milliseconds duration) override;
    size_t taskCount() const override;

    void reschedule(std::shared_ptr<TestEventLoopImpl::TestTimer> timer,
                    std::chrono::milliseconds new_delay, std::chrono::milliseconds new_interval);

  private:
    struct ScheduledTask {
        std::chrono::steady_clock::time_point execution_time;
        std::chrono::milliseconds interval;
        std::shared_ptr<Task> task;

        // handle to the timer that is handed to the developer
        // we track the liveness and cancellation state here.
        std::weak_ptr<TestTimer> handle;

        bool operator>(const ScheduledTask& other) const {
            return execution_time > other.execution_time;
        }
    };

    class TestTimer : public Timer, public std::enable_shared_from_this<TestTimer> {
      public:
        TestTimer(TestEventLoopImpl* loop, Task task)
                : mLoop(loop), mPendingTask(std::make_shared<Task>(std::move(task))) {}
        ~TestTimer() override { cancel(); }
        void cancel() override { mCancelled = true; }
        bool isCancelled() const { return mCancelled; }
        std::shared_ptr<Task> task() { return mPendingTask; }
        void schedule(std::chrono::milliseconds new_delay,
                      std::chrono::milliseconds new_interval) override {
            mLoop->reschedule(shared_from_this(), new_delay, new_interval);
        }

      private:
        std::atomic_bool mCancelled{false};
        TestEventLoopImpl* mLoop;
        std::shared_ptr<Task> mPendingTask;
    };

    enum class Command : uint8_t { None, RunOne, RunMany, AdvanceTime };

    void loop();
    bool runOneUnlocked();
    void advanceClockUnlocked(std::chrono::milliseconds duration);

    std::thread mThread;
    std::thread::id mThreadId;
    std::atomic<bool> mStop{false};
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::condition_variable mCmdCv;

    // post queue
    std::deque<Task> mTasks;

    // scheduled things
    std::vector<ScheduledTask> mScheduledTasks;
    std::chrono::steady_clock::time_point mNow;
    Command mCommand = Command::None;
    std::chrono::milliseconds mTimeAdvance{0};
    size_t mRunCount = 0;
    size_t mTasksActuallyRun = 0;
};

// --- Factory Function ---
std::unique_ptr<TestEventLoop> TestEventLoop::create() {
    return std::make_unique<TestEventLoopImpl>();
}

// --- TestEventLoopImpl Implementation ---
TestEventLoopImpl::TestEventLoopImpl() : mNow(std::chrono::steady_clock::now()) {
    std::promise<void> thread_started_promise;
    auto thread_started_future = thread_started_promise.get_future();
    mThread = std::thread([this, &thread_started_promise]() {
        mThreadId = std::this_thread::get_id();
        setState(LooperStatusEvent::State::RUNNING);
        thread_started_promise.set_value();
        loop();
    });
    thread_started_future.wait();
}

TestEventLoopImpl::~TestEventLoopImpl() {
    if (getState() != LooperStatusEvent::State::SHUTTING_DOWN) {
        shutdownAndWait();
    }
    mStop = true;
    mCv.notify_one();
    if (mThread.joinable()) {
        mThread.join();
    }
}

std::future<absl::Status> TestEventLoopImpl::shutdown() {
    setState(LooperStatusEvent::State::SHUTTING_DOWN);
    std::promise<absl::Status> promise;
    promise.set_value(absl::OkStatus());
    std::lock_guard<std::mutex> lock(mMutex);
    mTasks.clear();
    mScheduledTasks.clear();

    return promise.get_future();
}

bool TestEventLoopImpl::isOnLoopThread() const {
    return std::this_thread::get_id() == mThreadId;
}

absl::Status TestEventLoopImpl::postImmediately(Task task) {
    if (getState() == LooperStatusEvent::State::SHUTTING_DOWN) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mTasks.emplace_back(std::move(task));
    return absl::OkStatus();
}

absl::Status TestEventLoopImpl::postDelayed(Task task, std::chrono::milliseconds delay) {
    if (getState() == LooperStatusEvent::State::SHUTTING_DOWN) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    auto timer = createTimer(std::move(task));
    timer->schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

size_t TestEventLoopImpl::taskCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mTasks.size();
}

std::shared_ptr<EventLoop::Timer> TestEventLoopImpl::createTimer(Task task) {
    return std::make_shared<TestTimer>(this, std::move(task));
}

void TestEventLoopImpl::reschedule(std::shared_ptr<TestTimer> timer,
                                   std::chrono::milliseconds new_delay,
                                   std::chrono::milliseconds new_interval) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = std::find_if(mScheduledTasks.begin(), mScheduledTasks.end(),
                           [&](const ScheduledTask& task) {
                               auto handle = task.handle.lock();
                               return handle && handle.get() == timer.get();
                           });

    if (it != mScheduledTasks.end()) {
        it->execution_time = mNow + new_delay;
        it->interval = new_interval;
        std::make_heap(mScheduledTasks.begin(), mScheduledTasks.end(), std::greater<>{});
    } else {
        mScheduledTasks.push_back({mNow + new_delay, new_interval, timer->task(), timer});
        std::push_heap(mScheduledTasks.begin(), mScheduledTasks.end(), std::greater<>{});
    }
}

void TestEventLoopImpl::runAll() {
    runMany(std::numeric_limits<size_t>::max());
}

bool TestEventLoopImpl::runOne() {
    std::unique_lock<std::mutex> lock(mMutex);
    mCommand = Command::RunOne;
    mTasksActuallyRun = 0;
    mCv.notify_one();
    mCmdCv.wait(lock, [this] { return mCommand == Command::None; });
    return mTasksActuallyRun > 0;
}

size_t TestEventLoopImpl::runMany(size_t count) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCommand = Command::RunMany;
    mRunCount = count;
    mTasksActuallyRun = 0;
    mCv.notify_one();
    mCmdCv.wait(lock, [this] { return mCommand == Command::None; });
    return mTasksActuallyRun;
}

void TestEventLoopImpl::advanceClock(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(mMutex);
    mCommand = Command::AdvanceTime;
    mTimeAdvance = duration;
    mCv.notify_one();
    mCmdCv.wait(lock, [this] { return mCommand == Command::None; });
}

void TestEventLoopImpl::loop() {
    std::unique_lock<std::mutex> lock(mMutex);
    while (!mStop) {
        mCv.wait(lock, [this] { return mCommand != Command::None || mStop; });
        if (mStop) break;

        // Note, we have the mutex here.
        switch (mCommand) {
        case Command::RunOne:
            mTasksActuallyRun = runOneUnlocked() ? 1 : 0;
            break;
        case Command::RunMany:
            for (size_t i = 0; i < mRunCount; ++i) {
                if (runOneUnlocked()) {
                    mTasksActuallyRun++;
                } else {
                    break;  // No more tasks to run
                }
            }
            break;
        case Command::AdvanceTime:
            advanceClockUnlocked(mTimeAdvance);
            break;
        case Command::None:
            break;
        }

        mCommand = Command::None;
        mCmdCv.notify_one();
    }
    setState(LooperStatusEvent::State::FINISHED);
}

bool TestEventLoopImpl::runOneUnlocked() {
    // we have the mutex here.
    if (mTasks.empty()) {
        return false;
    }
    Task task_to_run = std::move(mTasks.front());
    mTasks.pop_front();
    mMutex.unlock();
    // without lock so tasks can schedule more tasks etc..
    task_to_run();
    mMutex.lock();
    return true;
}

void TestEventLoopImpl::advanceClockUnlocked(std::chrono::milliseconds duration) {
    mNow += duration;
    std::vector<ScheduledTask> tasks_to_run;

    // Pop all tasks from the heap that are ready to go
    while (!mScheduledTasks.empty() && mScheduledTasks.front().execution_time <= mNow) {
        std::pop_heap(mScheduledTasks.begin(), mScheduledTasks.end(), std::greater<>{});
        tasks_to_run.push_back(std::move(mScheduledTasks.back()));
        mScheduledTasks.pop_back();
    }

    for (auto& task : tasks_to_run) {
        auto handle = task.handle.lock();
        if (!handle || handle->isCancelled()) {
            continue;
        }

        // Run the task without a lock.
        mMutex.unlock();
        (*task.task)();
        mMutex.lock();

        // reschedule task if needed.
        if (task.interval > std::chrono::milliseconds(0)) {
            task.execution_time += task.interval;
            mScheduledTasks.push_back(std::move(task));
            std::push_heap(mScheduledTasks.begin(), mScheduledTasks.end(), std::greater<>{});
        }
    }
}

}  // namespace goldfish::async::testing
