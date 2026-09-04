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

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/threads/thread_utils.h"

namespace goldfish::async::testing {

// The concrete implementation class, hidden entirely within this .cpp file.
class TestEventLoopImpl : public TestEventLoop {
    class TestTimer;

  public:
    explicit TestEventLoopImpl(std::string name);
    ~TestEventLoopImpl() override;

    // EventLoop Interface
    void ShutdownTimers() override;
    size_t WaitUntilIdle() override;
    std::future<absl::Status> Shutdown() override;
    bool IsOnLoopThread() const override;
    absl::Status PostImmediately(Task task, FlowId flow_id) override;
    absl::Status PostDelayed(Task task, absl::Duration delay, FlowId flow_id) override;
    std::shared_ptr<Timer> CreateTimer(RepeatingTask task) override;

    // TestEventLoop Interface
    void RunAll() override;
    bool RunOne() override;
    size_t RunMany(size_t count) override;
    void AdvanceClock(absl::Duration duration) override;
    size_t TaskCount() const override;

    void Reschedule(std::shared_ptr<TestEventLoopImpl::TestTimer> timer, absl::Duration new_delay,
                    absl::Duration new_interval);

  private:
    struct QueuedTask {
        Task task;
        FlowId flow_id;
    };

    struct ScheduledTask {
        absl::Time execution_time;
        absl::Duration interval;
        std::shared_ptr<RepeatingTask> task;

        // handle to the timer that is handed to the developer
        // we track the liveness and cancellation state here.
        std::weak_ptr<TestTimer> handle;
        FlowId flow_id = 0;

        bool operator>(const ScheduledTask& other) const {
            return execution_time > other.execution_time;
        }
    };

    class TestTimer : public Timer, public std::enable_shared_from_this<TestTimer> {
      public:
        TestTimer(TestEventLoopImpl* loop, RepeatingTask task, FlowId flow_id = 0)
                : loop_(loop)
                , pending_task_(std::make_shared<RepeatingTask>(std::move(task)))
                , flow_id_(flow_id) {}
        ~TestTimer() override { Cancel(); }
        void Cancel() override { cancelled_ = true; }
        bool IsCancelled() const { return cancelled_; }
        std::shared_ptr<RepeatingTask> task() { return pending_task_; }  // NOLINT
        FlowId GetFlowId() const { return flow_id_; }
        void Schedule(absl::Duration new_delay, absl::Duration new_interval) override {
            loop_->Reschedule(shared_from_this(), new_delay, new_interval);
        }

      private:
        std::atomic_bool cancelled_{false};
        TestEventLoopImpl* loop_;
        std::shared_ptr<RepeatingTask> pending_task_;
        ::goldfish::async::FlowId flow_id_;
    };

    enum class Command : uint8_t { kNone, kRunOne, kRunMany, kAdvanceTime };

    void Loop();
    bool RunOneUnlocked() ABSL_NO_THREAD_SAFETY_ANALYSIS;
    void AdvanceClockUnlocked(absl::Duration duration) ABSL_NO_THREAD_SAFETY_ANALYSIS;

    std::thread thread_;
    std::thread::id thread_id_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cmd_cv_;
    std::condition_variable queue_is_idle_cv_;

    // post queue
    std::deque<QueuedTask> tasks_;

    // scheduled things
    std::vector<ScheduledTask> scheduled_tasks_;
    absl::Time now_;
    Command command_ = Command::kNone;
    absl::Duration time_advance_{absl::ZeroDuration()};
    size_t run_count_ = 0;
    size_t tasks_actually_run_ = 0;
    size_t tasks_processed_ = 0;
    bool queue_is_idle_ = true;
};

// --- Factory Function ---
std::unique_ptr<TestEventLoop> TestEventLoop::Create(std::string name) {
    return std::make_unique<TestEventLoopImpl>(std::move(name));
}

// --- TestEventLoopImpl Implementation ---
TestEventLoopImpl::TestEventLoopImpl(std::string name)
        : TestEventLoop(std::move(name)), now_(absl::Now()) {
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

void TestEventLoopImpl::ShutdownTimers() {
    const std::lock_guard<std::mutex> lock(mutex_);
    scheduled_tasks_.clear();
}

size_t TestEventLoopImpl::WaitUntilIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_is_idle_cv_.wait(lock, [this]() { return queue_is_idle_; });
    return tasks_processed_;
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

absl::Status TestEventLoopImpl::PostImmediately(Task task, FlowId flow_id) {
    if (GetState() == LooperStatusEvent::State::kShuttingDown) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    queue_is_idle_ = false;
    tasks_.push_back(QueuedTask{std::move(task), flow_id});
    return absl::OkStatus();
}

absl::Status TestEventLoopImpl::PostDelayed(Task task, absl::Duration delay, FlowId flow_id) {
    if (GetState() == LooperStatusEvent::State::kShuttingDown) {
        LOG(ERROR) << "Loop is shutting down.";
        return absl::UnavailableError("test loop is shutting down");
    }
    auto timer = std::make_shared<TestTimer>(
            this,
            [task = std::move(task)]() mutable {
                task();
                return false;
            },
            flow_id);
    timer->Schedule(delay, absl::ZeroDuration());
    return absl::OkStatus();
}

size_t TestEventLoopImpl::TaskCount() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

std::shared_ptr<EventLoop::Timer> TestEventLoopImpl::CreateTimer(RepeatingTask task) {
    return std::make_shared<TestTimer>(this, std::move(task), 0);
}

void TestEventLoopImpl::Reschedule(std::shared_ptr<TestTimer> timer, absl::Duration new_delay,
                                   absl::Duration new_interval) {
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
        scheduled_tasks_.push_back(
                {now_ + new_delay, new_interval, timer->task(), timer, timer->GetFlowId()});
        std::ranges::push_heap(scheduled_tasks_, std::greater<>{});
    }
}

void TestEventLoopImpl::RunAll() {
    while (RunMany(std::numeric_limits<size_t>::max())) {
    }
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

void TestEventLoopImpl::AdvanceClock(absl::Duration duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    command_ = Command::kAdvanceTime;
    time_advance_ = duration;
    cv_.notify_one();
    cmd_cv_.wait(lock, [this] { return command_ == Command::kNone; });
}

void TestEventLoopImpl::Loop() {
    android::base::ThreadUtils::SetCurrentThreadName(GetName());
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

bool TestEventLoopImpl::RunOneUnlocked() ABSL_NO_THREAD_SAFETY_ANALYSIS {
    // we have the mutex here.
    if (tasks_.empty()) {
        return false;
    }
    QueuedTask queued = std::move(tasks_.front());
    tasks_.pop_front();
    mutex_.unlock();
    {
        // without lock so tasks can schedule more tasks etc..
        QueuedTask queued_scoped = std::move(queued);
        if (queued_scoped.flow_id != 0 && tracker()) {
            tracker()->LogExecute(queued_scoped.flow_id);
        }
        queued_scoped.task();
    }
    mutex_.lock();
    ++tasks_processed_;
    if (tasks_.empty()) {
        queue_is_idle_ = true;
        queue_is_idle_cv_.notify_all();
    }
    return true;
}

void TestEventLoopImpl::AdvanceClockUnlocked(absl::Duration duration)
        ABSL_NO_THREAD_SAFETY_ANALYSIS {
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
        if (task.flow_id != 0 && tracker()) {
            tracker()->LogExecute(task.flow_id);
        }
        const bool keep_repeating = (*task.task)();
        mutex_.lock();

        // Reschedule task if needed.
        if (!keep_repeating) {
            handle->Cancel();
        } else if (task.interval > absl::ZeroDuration()) {
            task.execution_time += task.interval;
            scheduled_tasks_.push_back(std::move(task));
            std::ranges::push_heap(scheduled_tasks_, std::greater<>{});
        }
    }
}

}  // namespace goldfish::async::testing
