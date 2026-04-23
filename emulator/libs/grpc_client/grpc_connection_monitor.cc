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

#include "grpc_connection_monitor.h"

#include <grpcpp/grpcpp.h>

#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace android::emulation::control {
namespace {

std::ostream& operator<<(std::ostream& os, ConnectionState state) {
    switch (state) {
    case ConnectionState::kDisconnected:
        return os << "Disconnected";
    case ConnectionState::kConnecting:
        return os << "Connecting";
    case ConnectionState::kConnected:
        return os << "Connected";
    }
    return os;
}

}  // namespace

struct GrpcConnectionMonitor::State {
    explicit State(const std::shared_ptr<grpc::Channel>& channel) : channel(channel) {}

    void SetConnectionState(ConnectionState state) {
        connection_state = state;
        state_changes.FireEvent(state);
    }

    std::weak_ptr<grpc::Channel> channel;

    std::thread worker_thread;
    std::thread::id worker_thread_id;
    std::atomic<bool> shutting_down{false};

    absl::Mutex callback_active;
    grpc::CompletionQueue completion_queue;

    ConnectionState connection_state{ConnectionState::kDisconnected};
    android::base::eventing::CallbackEventSource<ConnectionState> state_changes;
};

namespace {
// A self-owning callback that monitors the gRPC channel's state.
// It uses a state machine to transition between initial connection,
// liveness monitoring, and shutdown.
class MonitorCallback : public std::enable_shared_from_this<MonitorCallback> {
  public:
    MonitorCallback(std::shared_ptr<GrpcConnectionMonitor::State> s, std::promise<absl::Status> p,
                    gpr_timespec d)
            : monitor_state_(std::move(s)), promise_(std::move(p)), deadline_(d) {
        VLOG(1) << "Monitor callback created.";
    }

    void Arm(const std::shared_ptr<grpc::Channel>& channel,
             grpc_connectivity_state connectivity_state, gpr_timespec d) {
        DCHECK(self_ == nullptr);
        self_ = shared_from_this();
        channel->NotifyOnStateChange(connectivity_state, d, &monitor_state_->completion_queue,
                                     this);
    }

    void Run(bool ok) {
        DCHECK(self_ != nullptr);
        const auto guard = std::move(self_);
        if (!ok) {
            // This case should be reflected by a more specific error state returned by GetState
            // below.
            VLOG(1) << "monitor callback completion_queue next not ok";
        }
        if (monitor_state_->shutting_down.load(std::memory_order_acquire)) {
            if (!connection_established_) {
                CompletePromise(absl::CancelledError("monitor shutting down"));
            }
            return;
        }
        auto channel = monitor_state_->channel.lock();
        if (!channel) {
            if (!connection_established_) {
                CompletePromise(absl::CancelledError("channel destroyed"));
            }
            return;
        }

        if (!connection_established_) {
            HandleInitial(channel);
        } else {
            HandleMonitoring(channel);
        }
    }

  private:
    void HandleInitial(const std::shared_ptr<grpc::Channel>& channel) {
        auto connectivity_state = channel->GetState(/*try_to_connect=*/true);
        if (connectivity_state == GRPC_CHANNEL_READY) {
            monitor_state_->SetConnectionState(ConnectionState::kConnected);
            CompletePromise(absl::OkStatus());

            if (monitor_state_->shutting_down.load(std::memory_order_acquire)) {
                return;
            }

            // Transition to long-term monitoring.
            connection_established_ = true;
            Arm(channel, connectivity_state, gpr_inf_future(GPR_CLOCK_REALTIME));
        } else if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                   connectivity_state == GRPC_CHANNEL_SHUTDOWN ||
                   gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline_) >= 0) {
            monitor_state_->SetConnectionState(ConnectionState::kDisconnected);

            const bool timed_out = gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline_) >= 0;
            CompletePromise(timed_out ? absl::DeadlineExceededError("Connection timed out.")
                                      : absl::UnavailableError("Connection failed."));
        } else {
            // Keep trying.
            Arm(channel, connectivity_state, deadline_);
        }
    }

    void HandleMonitoring(const std::shared_ptr<grpc::Channel>& channel) {
        auto connectivity_state = channel->GetState(/*try_to_connect=*/false);
        if (connectivity_state != GRPC_CHANNEL_READY) {
            monitor_state_->SetConnectionState(ConnectionState::kDisconnected);
        } else {
            // Spurious notification, re-arm the monitor.
            Arm(channel, GRPC_CHANNEL_READY, gpr_inf_future(GPR_CLOCK_REALTIME));
        }
    }

    void CompletePromise(absl::Status status) {
        promise_.set_value(std::move(status));
    }

    std::shared_ptr<GrpcConnectionMonitor::State> monitor_state_;
    std::promise<absl::Status> promise_;
    gpr_timespec deadline_;

    bool connection_established_ = false;
    std::shared_ptr<MonitorCallback> self_;
};

}  // namespace

GrpcConnectionMonitor::GrpcConnectionMonitor(const std::shared_ptr<grpc::Channel>& channel)
        : state_(std::make_shared<State>(channel)) {
    state_->worker_thread = std::thread([state = state_]() {
        void* tag;
        bool ok;
        while (state->completion_queue.Next(&tag, &ok)) {
            auto* callback = static_cast<MonitorCallback*>(tag);
            const absl::MutexLock lock(state->callback_active);
            callback->Run(ok);
        }
        VLOG(1) << "Monitor worker thread finished.";
    });
    state_->worker_thread_id = state_->worker_thread.get_id();
}

GrpcConnectionMonitor::~GrpcConnectionMonitor() {
    Stop();
}

void GrpcConnectionMonitor::Stop() {
    if (std::this_thread::get_id() == state_->worker_thread_id) {
        LOG(ERROR) << "You cannot Stop the monitor thread from the monitor thread.";
        return;
    }

    if (state_->shutting_down.exchange(true)) {
        return;
    }

    {
        const absl::MutexLock lock(state_->callback_active);
        state_->completion_queue.Shutdown();
    }
    state_->worker_thread.join();
}

std::future<absl::Status> GrpcConnectionMonitor::Watch(absl::Duration timeout) {
    const auto deadline =
            gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_millis(absl::ToInt64Milliseconds(timeout), GPR_TIMESPAN));

    std::promise<absl::Status> p;
    auto future = p.get_future();

    if (auto channel = state_->channel.lock()) {
        auto callback = std::make_shared<MonitorCallback>(state_, std::move(p), deadline);
        // Trigger the connection process with true.
        callback->Arm(channel, channel->GetState(true), deadline);
    } else {
        p.set_value(absl::CancelledError("Channel is gone."));
    }

    return future;
}

android::base::eventing::CallbackEventSource<ConnectionState>&
GrpcConnectionMonitor::state_changes() {
    return state_->state_changes;
}

}  // namespace android::emulation::control
