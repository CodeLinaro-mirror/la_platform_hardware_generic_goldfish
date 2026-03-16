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
    std::future<absl::Status> Shutdown() override;
    bool IsOnLoopThread() const override;
    absl::Status PostImmediately(Task task) override;
    absl::Status PostDelayed(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> CreateTimer(Task task) override;

    // TestEventLoop Interface
    void RunAll() override;
    bool RunOne() override;
    size_t RunMany(size_t count) override;
    void AdvanceClock(std::chrono::milliseconds duration) override;
    size_t TaskCount() const override;

    void Reschedule(std::shared_ptr<TestEventLoopImpl::TestTimer> timer,
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
                : loop_(loop), pending_task_(std::make_shared<Task>(std::move(task))) {}
        ~TestTimer() override { Cancel(); }
        void Cancel() override { cancelled_ = true; }
        bool IsCancelled() const { return cancelled_; }
        std::shared_ptr<Task> task() { return pending_task_; }  // NOLINT
        void Schedule(std::chrono::milliseconds new_delay,
                      std::chrono::milliseconds new_interval) override {
            loop_->Reschedule(shared_from_this(), new_delay, new_interval);
        }

      private:
        std::atomic_bool cancelled_{false};
        TestEventLoopImpl* loop_;
        std::shared_ptr<Task> pending_task_;
    };

    enum class Command : uint8_t { kNone, kRunOne, kRunMany, kAdvanceTime };

    void Loop();
    bool RunOneUnlocked();
    void AdvanceClockUnlocked(std::chrono::milliseconds duration);

    std::thread thread_;
    std::thread::id thread_id_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cmd_cv_;

    // post queue
    std::deque<Task> tasks_;

    // scheduled things
    std::vector<ScheduledTask> scheduled_tasks_;
    std::chrono::steady_clock::time_point now_;
    Command command_ = Command::kNone;
    std::chrono::milliseconds time_advance_{0};
    size_t run_count_ = 0;
    size_t tasks_actually_run_ = 0;
};

// --- Factory Function ---
std::unique_ptr<TestEventLoop> TestEventLoop::Create() {
    return std::make_unique<TestEventLoopImpl>();
}

// --- TestEventLoopImpl Implementation ---
TestEventLoopImpl::TestEventLoopImpl() : now_(std::chrono::steady_clock::now()) {
    std::promise<void> thread_started_promise;
    auto thread_started_future = thread_started_promise.get_future();
    thread_ = std::thread([this, &thread_started_promise]() {
        thread_id_ = std::this_thread::get_id();
        SetState(LooperStatusEvent::State::kRunning);
        thread_started_promise.set_value();
        Loop();
    });
    thread_started_future.wait();
}

TestEventLoopImpl::~TestEventLoopImpl() {
    if (GetState() != LooperStatusEvent::State::kShuttingDown) {
        ShutdownAndWait().IgnoreError();
    }
    stop_ = true;
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::future<absl::Status> TestEventLoopImpl::Shutdown() {
    SetState(LooperStatusEvent::State::kShuttingDown);
    std::promise<absl::Status> promise;
    promise.set_value(absl::OkStatus());
    const std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
    scheduled_tasks_.clear();

    return promise.get_future();
}

bool TestEventLoopImpl::IsOnLoopThread() const {
    return std::this_thread::get_id() == thread_id_;
}

absl::Status TestEventLoopImpl::PostImmediately(Task task) {
    if (GetState() == LooperStatusEvent::State::kShuttingDown) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    tasks_.emplace_back(std::move(task));
    return absl::OkStatus();
}

absl::Status TestEventLoopImpl::PostDelayed(Task task, std::chrono::milliseconds delay) {
    if (GetState() == LooperStatusEvent::State::kShuttingDown) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    auto timer = CreateTimer(std::move(task));
    timer->Schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

size_t TestEventLoopImpl::TaskCount() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::shared_ptr<EventLoop::Timer> TestEventLoopImpl::CreateTimer(Task task) {
    return std::make_shared<TestTimer>(this, std::move(task));
}

void TestEventLoopImpl::Reschedule(std::shared_ptr<TestTimer> timer,
                                   std::chrono::milliseconds new_delay,
                                   std::chrono::milliseconds new_interval) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::ranges::find_if(scheduled_tasks_, [&](const ScheduledTask& task) {
        auto handle = task.handle.lock();
        return handle && handle.get() == timer.get();
    });

    if (it != scheduled_tasks_.end()) {
        it->execution_time = now_ + new_delay;
        it->interval = new_interval;
        std::ranges::make_heap(scheduled_tasks_, std::greater<>{});
    } else {
        scheduled_tasks_.push_back({now_ + new_delay, new_interval, timer->task(), timer});
        std::ranges::push_heap(scheduled_tasks_, std::greater<>{});
    }
}

void TestEventLoopImpl::RunAll() {
    RunMany(std::numeric_limits<size_t>::max());
}

bool TestEventLoopImpl::RunOne() {
    std::unique_lock<std::mutex> lock(mutex_);
    command_ = Command::kRunOne;
    tasks_actually_run_ = 0;
    cv_.notify_one();
    cmd_cv_.wait(lock, [this] { return command_ == Command::kNone; });
    return tasks_actually_run_ > 0;
}

size_t TestEventLoopImpl::RunMany(size_t count) {
    std::unique_lock<std::mutex> lock(mutex_);
    command_ = Command::kRunMany;
    run_count_ = count;
    tasks_actually_run_ = 0;
    cv_.notify_one();
    cmd_cv_.wait(lock, [this] { return command_ == Command::kNone; });
    return tasks_actually_run_;
}

void TestEventLoopImpl::AdvanceClock(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    command_ = Command::kAdvanceTime;
    time_advance_ = duration;
    cv_.notify_one();
    cmd_cv_.wait(lock, [this] { return command_ == Command::kNone; });
}

void TestEventLoopImpl::Loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        cv_.wait(lock, [this] { return command_ != Command::kNone || stop_; });
        if (stop_) break;

        // Note, we have the mutex here.
        switch (command_) {
        case Command::kRunOne:
            tasks_actually_run_ = RunOneUnlocked() ? 1 : 0;
            break;
        case Command::kRunMany:
            for (size_t i = 0; i < run_count_; ++i) {
                if (RunOneUnlocked()) {
                    tasks_actually_run_++;
                } else {
                    break;  // No more tasks to run
                }
            }
            break;
        case Command::kAdvanceTime:
            AdvanceClockUnlocked(time_advance_);
            break;
        case Command::kNone:
            break;
        }

        command_ = Command::kNone;
        cmd_cv_.notify_one();
    }
    SetState(LooperStatusEvent::State::kFinished);
}

bool TestEventLoopImpl::RunOneUnlocked() {
    // we have the mutex here.
    if (tasks_.empty()) {
        return false;
    }
    Task task_to_run = std::move(tasks_.front());
    tasks_.pop_front();
    mutex_.unlock();
    {
        // without lock so tasks can schedule more tasks etc..
        Task task_to_run_scoped(std::move(task_to_run));
        task_to_run_scoped();
        // ~Task for the original task is called here
    }
    mutex_.lock();
    return true;
}

void TestEventLoopImpl::AdvanceClockUnlocked(std::chrono::milliseconds duration) {
    now_ += duration;
    std::vector<ScheduledTask> tasks_to_run;

    // Pop all tasks from the heap that are ready to go
    while (!scheduled_tasks_.empty() && scheduled_tasks_.front().execution_time <= now_) {
        std::ranges::pop_heap(scheduled_tasks_, std::greater<>{});
        tasks_to_run.push_back(std::move(scheduled_tasks_.back()));
        scheduled_tasks_.pop_back();
    }

    for (auto& task : tasks_to_run) {
        auto handle = task.handle.lock();
        if (!handle || handle->IsCancelled()) {
            continue;
        }

        // Run the task without a lock.
        mutex_.unlock();
        (*task.task)();
        mutex_.lock();

        // Reschedule task if needed.
        if (task.interval > std::chrono::milliseconds(0)) {
            task.execution_time += task.interval;
            scheduled_tasks_.push_back(std::move(task));
            std::ranges::push_heap(scheduled_tasks_, std::greater<>{});
        }
    }
}

}  // namespace goldfish::async::testing
