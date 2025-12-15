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
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "aemu/base/events/EventSources.h"

namespace goldfish::async {

using android::base::eventing::CallbackEventSource;

/**
 * @brief Event that is fired when the state of the looper changes.
 * Observers can subscribe to this event to be notified of the looper's
 * lifecycle.
 */
struct LooperStatusEvent {
    enum class State : uint8_t {
        kNotStarted,    // The loop has not yet been started.
        kRunning,       // The loop is actively processing events.
        kShuttingDown,  // A graceful shutdown has been initiated.
        kFinished,      // The loop has finished execution.
    };

    State state;
};

template <typename Sink>
void AbslStringify(Sink& sink, const LooperStatusEvent& event) {
    switch (event.state) {
    case LooperStatusEvent::State::kNotStarted:
        sink.Append("NOT_STARTED");
        break;
    case LooperStatusEvent::State::kRunning:
        sink.Append("RUNNING");
        break;
    case LooperStatusEvent::State::kShuttingDown:
        sink.Append("SHUTTING_DOWN");
        break;
    case LooperStatusEvent::State::kFinished:
        sink.Append("FINISHED");
        break;
    }
}

/**
 * @brief An abstract interface for an event loop.
 *
 * This allows application code to depend on the concept of an event loop
 * without being tied to a specific implementation like libuv or asio.
 */
class EventLoop : public CallbackEventSource<LooperStatusEvent> {
  public:
    /**
     * @brief A move-only, type-erased unit of work to be executed.
     *
     * Using absl::AnyInvocable allows the EventLoop to accept any callable,
     * including move-only lambdas (e.g., those capturing a std::unique_ptr),
     * without the overhead or copy restrictions of std::function.
     */
    using Task = absl::AnyInvocable<void()>;

    /**
     * @brief An opaque handle to a scheduled task.
     *
     * Allows for the scheduling and cancellation of a delayed or repeating task.
     */
    class Timer {
      public:
        virtual ~Timer() = default;
        /**
         * @brief Cancels the scheduled task.
         * If the task has already run or been cancelled, this is a no-op.
         */
        virtual void Cancel() = 0;

        /**
         * @brief (Re)Schedules a timer with a new delay and interval.
         *
         * This method updates both the delay for the next execution and the
         * subsequent interval for the timer. This does nothing if the timer has
         * already been cancelled. Likewise if the event loop has shutdown.
         *
         * @param new_delay The new delay before the next execution.
         * @param new_interval The new interval for subsequent executions. When
         * set to 0 the timer will not repeat.
         */
        virtual void Schedule(std::chrono::milliseconds new_delay,
                              std::chrono::milliseconds new_interval) = 0;

        /**
         * @brief (Re)Schedules a timer with a new delay.
         *
         * This method updates the delay for the next execution. This does
         * nothing if the timer has already been cancelled. Likewise if the
         * event loop has shutdown.
         *
         * @param new_delay The new delay before the next execution.
         */
        void Schedule(std::chrono::milliseconds new_delay) {
            Schedule(new_delay, std::chrono::milliseconds::zero());
        }
    };

    virtual ~EventLoop() = default;

    absl::Status ShutdownAndWait(
            std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto future = Shutdown();
        if (timeout != std::chrono::milliseconds::zero()) {
            if (future.wait_for(timeout) != std::future_status::ready) {
                return absl::DeadlineExceededError(
                        "Loop shutdown did not return a result within the deadline");
            }
        }
        return future.get();
    }

    /**
     * @brief Initiates a graceful shutdown of the event loop.
     *
     * This method schedules the closing of all internal handles. It returns
     * a future that will be fulfilled when all cleanup tasks are complete.
     * This should be called before the object is destroyed.
     *
     * Note: that this will cancel all outstanding timers and posted callbacks
     * once this returns no new timers are callbacks can be scheduled.
     */
    virtual std::future<absl::Status> Shutdown() = 0;

    /**
     * @brief Checks if the current thread is the one running this event loop.
     * @return true if the caller is on the event loop's thread, false otherwise.
     */
    virtual bool IsOnLoopThread() const = 0;

    /**
     * @brief Posts a callable object for execution on the event loop.
     *
     * @tparam F The type of the callable object.
     * @param f The callable object to execute.
     * @param delay The duration to wait before executing the task. A delay of
     * zero executes the task as soon as possible.
     * @return A std::future that will be fulfilled with the return value of the task.
     */
    template <typename F>
    auto Post(F&& f, std::chrono::milliseconds delay = std::chrono::milliseconds::zero())
            -> absl::StatusOr<std::future<decltype(std::forward<F>(f)())>> {
        using ReturnType = decltype(std::forward<F>(f)());
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();

        // This lambda will be executed on the event loop thread.
        auto task_runner = [promise, f = std::forward<F>(f)]() mutable {
            if constexpr (std::is_void_v<ReturnType>) {
                f();
                promise->set_value();
            } else {
                promise->set_value(f());
            }
        };

        absl::Status s;
        if (delay == std::chrono::milliseconds::zero()) {
            s = PostImmediately(std::move(task_runner));
        } else {
            s = PostDelayed(std::move(task_runner), delay);
        }
        if (!s.ok()) {
            return s;
        }

        return future;
    }

    /**
     * @brief Posts a task to the event loop and blocks the calling thread
     * until the task is complete.
     *
     * @warning This method MUST NOT be called from the event loop's own
     * thread, as it will cause an immediate deadlock. The application
     * will immediately exit with a FATAL warning.
     */
    template <typename F>
    auto PostAndWait(F&& task) -> std::conditional_t<std::is_void_v<decltype(task())>, absl::Status,
                                                     absl::StatusOr<decltype(task())>> {
        if (IsOnLoopThread()) {
            LOG(FATAL) << "postAndWait cannot be called from the event loop.";
        }

        if (auto future = Post<F>(std::forward<F>(task)); future.ok()) {
            if constexpr (std::is_void_v<decltype(task())>) {
                future->get();
                return absl::OkStatus();
            } else {
                return future->get();
            }
        } else {
            return future.status();
        }
    }

    /**
     * @brief Schedules a cancellable task to be executed once after a delay.
     * @param task The task to execute.
     * @return A shared pointer to a Timer handle for scheduling and cancellation.
     */
    virtual std::shared_ptr<Timer> CreateTimer(Task task) = 0;

    /**
     * @brief Schedules a cancellable task to be executed once after a delay.
     * @param task The task to execute.
     * @param delay The duration to wait before executing the task.
     * @return A shared pointer to a Timer handle for cancellation.
     */
    std::shared_ptr<Timer> ScheduleDelayed(Task task, std::chrono::milliseconds delay) {
        return ScheduleRepeating(std::move(task), delay, std::chrono::milliseconds::zero());
    }

    /**
     * @brief Schedules a cancellable task to be executed repeatedly.
     * @param task The task to execute.
     * @param initial_delay The delay before the first execution.
     * @param interval The time between subsequent executions.
     * @return A shared pointer to a Timer handle for cancellation.
     */
    std::shared_ptr<Timer> ScheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) {
        auto timer = CreateTimer(std::move(task));
        timer->Schedule(initial_delay, interval);
        return timer;
    };

    // Implementation specific loop.
    virtual void* GetRawLoop() {
        return nullptr;  // `nullptr` is a valid value here
    }

    /**
     * @brief Gets the current state of the event loop.
     * @return The current state.
     */
    virtual LooperStatusEvent::State GetState() const { return state_; }

  protected:
    virtual absl::Status PostImmediately(Task task) = 0;
    virtual absl::Status PostDelayed(Task task, std::chrono::milliseconds delay) = 0;

    void SetState(LooperStatusEvent::State new_state) {
        const LooperStatusEvent::State old_state = state_.exchange(new_state);
        if (old_state != new_state) {
            fireEvent({.state = new_state});
        }
    }

  private:
    std::atomic<LooperStatusEvent::State> state_{LooperStatusEvent::State::kNotStarted};
};

}  // namespace goldfish::async
