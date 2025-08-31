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

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace goldfish::async::testing {

// The concrete implementation class, hidden entirely within this .cpp file.
class TestEventLoopImpl : public TestEventLoop {
  public:
    TestEventLoopImpl();
    ~TestEventLoopImpl() override;

    // EventLoop Interface
    absl::Status run() override;
    void stop() override;
    std::future<absl::Status> shutdown(std::chrono::milliseconds timeout) override;
    bool isOnLoopThread() const override;
    void postImpl(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> scheduleDelayed(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> scheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) override;
    void* getRawLoop() const override;

    // TestEventLoop Interface
    void runAll() override;
    bool runOne() override;
    size_t runMany(size_t count) override;
    void advanceClock(std::chrono::milliseconds duration) override;
    size_t taskCount() const override;

  private:
    class TestTimer;
    struct ScheduledTask {
        std::chrono::steady_clock::time_point execution_time;
        std::chrono::milliseconds interval;
        Task task;

        // handle to the timer that is handed to the developer
        // we track the liveness and cancellation state here.
        std::weak_ptr<TestTimer> handle;

        bool operator>(const ScheduledTask& other) const {
            return execution_time > other.execution_time;
        }
    };

    class TestTimer : public Timer, public std::enable_shared_from_this<TestTimer> {
      public:
        ~TestTimer() override { cancel(); }
        void cancel() override { mCancelled = true; }
        bool isCancelled() const { return mCancelled; }

      private:
        std::atomic_bool mCancelled{false};
    };

    enum class Command : uint8_t { None, RunOne, RunMany, AdvanceTime };

    void loop();
    bool runOneUnlocked();
    void advanceClockUnlocked(std::chrono::milliseconds duration);

    std::thread mThread;
    std::thread::id mThreadId;
    std::atomic<bool> mStop{false};
    std::mutex mMutex;
    std::condition_variable mCv;
    std::condition_variable mCmdCv;

    // post queue
    std::deque<Task> mTasks;

    // scheduled things
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<>> mScheduledTasks;
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
    if (mThread.joinable()) {
        shutdown(std::chrono::milliseconds(0)).wait();
        stop();
        mThread.join();
    }
}

absl::Status TestEventLoopImpl::run() {
    return absl::OkStatus();
}

void TestEventLoopImpl::stop() {
    std::lock_guard<std::mutex> lock(mMutex);
    mStop = true;
    mCv.notify_one();
}

std::future<absl::Status> TestEventLoopImpl::shutdown(std::chrono::milliseconds) {
    setState(LooperStatusEvent::State::SHUTTING_DOWN);
    std::promise<absl::Status> promise;
    promise.set_value(absl::OkStatus());
    std::lock_guard<std::mutex> lock(mMutex);
    mTasks.clear();
    mScheduledTasks = {};
    return promise.get_future();
}

bool TestEventLoopImpl::isOnLoopThread() const {
    return std::this_thread::get_id() == mThreadId;
}

void TestEventLoopImpl::postImpl(Task task, std::chrono::milliseconds delay) {
    if (getState() == LooperStatusEvent::State::SHUTTING_DOWN) {
        LOG(ERROR) << "Loop is shutting down.";
        return;
    }
    if (delay == std::chrono::milliseconds(0)) {
        std::lock_guard<std::mutex> lock(mMutex);
        mTasks.emplace_back(std::move(task));
        return;
    }
    scheduleDelayed(std::move(task), delay);
}

size_t TestEventLoopImpl::taskCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mTasks.size();
}

std::shared_ptr<EventLoop::Timer> TestEventLoopImpl::scheduleDelayed(
        Task task, std::chrono::milliseconds delay) {
    if (getState() == LooperStatusEvent::State::SHUTTING_DOWN) return nullptr;
    auto timer = std::make_shared<TestTimer>();
    std::lock_guard<std::mutex> lock(mMutex);
    mScheduledTasks.push({mNow + delay, std::chrono::milliseconds(0), std::move(task), timer});
    return timer;
}

std::shared_ptr<EventLoop::Timer> TestEventLoopImpl::scheduleRepeating(
        Task task, std::chrono::milliseconds initial_delay, std::chrono::milliseconds interval) {
    if (getState() == LooperStatusEvent::State::SHUTTING_DOWN) return nullptr;
    auto timer = std::make_shared<TestTimer>();
    std::lock_guard<std::mutex> lock(mMutex);
    mScheduledTasks.push({mNow + initial_delay, interval, std::move(task), timer});
    return timer;
}

void* TestEventLoopImpl::getRawLoop() const {
    return nullptr;
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

    // Pop all tasks from the priority queue that are ready to go
    while (!mScheduledTasks.empty() && mScheduledTasks.top().execution_time <= mNow) {
        // Boo! our priority queue returns the top element as const! And our scheduled task
        // can *only* be moved due to the task inside scheduled task being move only.
        // (const objects cannot be moved, only copied.).  The priority queue does this so it
        // can enforce the internal ordering invariant (no one can modify elements in the queue).
        tasks_to_run.push_back(std::move(const_cast<ScheduledTask&>(mScheduledTasks.top())));
        mScheduledTasks.pop();
    }

    for (auto& task : tasks_to_run) {
        auto handle = task.handle.lock();
        if (!handle || handle->isCancelled()) {
            continue;
        }

        // Run the task without a lock.
        mMutex.unlock();
        task.task();
        mMutex.lock();

        // reschedule task if needed.
        if (task.interval > std::chrono::milliseconds(0)) {
            task.execution_time += task.interval;

            mScheduledTasks.push(std::move(task));
        }
    }
}

}  // namespace goldfish::async::testing