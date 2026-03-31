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

#include <cstdint>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace android::emulation::control {
namespace {

std::ostream& operator<<(std::ostream& os, ConnectionState state) {
    switch (state) {
    case ConnectionState::kDisconnected:
        os << "Disconnected";
        break;
    case ConnectionState::kConnecting:
        os << "Connecting";
        break;
    case ConnectionState::kConnected:
        os << "Connected";
        break;
    }
    return os;
}

}  // namespace

// A self-owning callback that monitors the gRPC channel's state.
// It uses a state machine to transition between initial connection,
// liveness monitoring, and shutdown.
struct GrpcConnectionMonitor::MonitorCallback {
    enum class State : std::uint8_t { kInitial, kMonitoring, kShutdown };

    State state = State::kInitial;
    GrpcConnectionMonitor* monitor;
    std::shared_ptr<std::promise<absl::Status>> promise;
    absl::Duration timeout;

    MonitorCallback(GrpcConnectionMonitor* m, std::shared_ptr<std::promise<absl::Status>> p,
                    absl::Duration t)
            : monitor(m), promise(std::move(p)), timeout(t) {
        VLOG(1) << "Monitor callback created, with promise";
    }

    void Run(bool ok) {
        VLOG(1) << "Running monitor callback";
        auto channel = monitor->channel_.lock();
        if (!channel || monitor->shutting_down_.load(std::memory_order_acquire)) {
            VLOG(1) << "Shutting down monitor";
            if (promise) {
                VLOG(1) << "Setting cancel promise";
                promise->set_value(absl::CancelledError("Connection cancelled."));
            }
            delete this;
            return;
        }

        const grpc_connectivity_state new_state = channel->GetState(state == State::kInitial);

        switch (state) {
        case State::kInitial:
            HandleInitial(ok, new_state);
            break;
        case State::kMonitoring:
            HandleMonitoring(ok, new_state);
            break;
        case State::kShutdown:
            delete this;
            break;
        }
    }

    void HandleInitial(bool /*ok*/, grpc_connectivity_state new_state) {
        auto channel = monitor->channel_.lock();
        if (!channel || monitor->shutting_down_.load(std::memory_order_acquire)) {
            delete this;
            return;
        }
        if (new_state == GRPC_CHANNEL_READY) {
            monitor->SetConnectionState(ConnectionState::kConnected);
            if (promise) {
                VLOG(1) << "Setting ok promise, monitor state: "
                        << (monitor->shutting_down_ ? "shutting down" : "active");
                promise->set_value(absl::OkStatus());
                promise.reset();  // Fulfill promise only once.
            }
            // Transition to long-term monitoring.
            state = State::kMonitoring;
            channel->NotifyOnStateChange(new_state, gpr_inf_future(GPR_CLOCK_REALTIME),
                                         &monitor->completion_queue_, this);
        } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                   new_state == GRPC_CHANNEL_SHUTDOWN) {
            monitor->SetConnectionState(ConnectionState::kDisconnected);
            if (promise) {
                VLOG(1) << "Setting connection failed promise";
                promise->set_value(absl::UnavailableError("Connection failed."));
            }
            delete this;  // End of the line.
        } else {
            // Keep trying.
            channel->NotifyOnStateChange(
                    new_state,
                    gpr_time_from_millis(absl::ToInt64Milliseconds(timeout), GPR_CLOCK_REALTIME),
                    &monitor->completion_queue_, this);
        }
    }

    void HandleMonitoring(bool ok, grpc_connectivity_state new_state) {
        auto channel = monitor->channel_.lock();
        if (!channel || monitor->shutting_down_.load(std::memory_order_acquire)) {
            delete this;
            return;
        }
        if (!ok || new_state != GRPC_CHANNEL_READY) {
            monitor->SetConnectionState(ConnectionState::kDisconnected);
            // End of the line for this monitor.
            delete this;
        } else {
            // Spurious notification, re-arm the monitor.
            channel->NotifyOnStateChange(GRPC_CHANNEL_READY, gpr_inf_future(GPR_CLOCK_REALTIME),
                                         &monitor->completion_queue_, this);
        }
    }
};

GrpcConnectionMonitor::GrpcConnectionMonitor(const std::shared_ptr<grpc::Channel>& channel)
        : channel_(channel) {
    worker_thread_ = std::thread(&GrpcConnectionMonitor::AsyncWorker, this);
    worker_thread_id_ = worker_thread_.get_id();
}

GrpcConnectionMonitor::~GrpcConnectionMonitor() {
    assert(std::this_thread::get_id() != worker_thread_id_ &&
           "The monitor thread should not be destroying the monitor!");
    Stop();
}

void GrpcConnectionMonitor::Stop() {
    if (std::this_thread::get_id() == worker_thread_id_) {
        VLOG(1) << "You cannot Stop the monitor thread from the monitor thread.";
        return;
    }

    if (shutting_down_.exchange(true)) {
        return;
    }
    VLOG(1) << "shutting_down_ set to: " << shutting_down_;
    if (worker_thread_.joinable()) {
        VLOG(1) << "Joining worker thread, we are disconnecting";
        {
            // Do not yank the queue out when a callback is active, callbacks could schedule
            // something which is not allowed after this call returns.
            const absl::MutexLock lock(callback_active_);
            completion_queue_.Shutdown();
        }
        worker_thread_.join();
    }
}

std::future<absl::Status> GrpcConnectionMonitor::Watch(absl::Duration timeout) {
    auto promise = std::make_shared<std::promise<absl::Status>>();
    auto future = promise->get_future();

    auto* callback = new MonitorCallback(this, promise, timeout);
    if (auto channel = channel_.lock()) {
        channel->NotifyOnStateChange(
                channel->GetState(false),
                gpr_time_from_millis(absl::ToInt64Milliseconds(timeout), GPR_CLOCK_REALTIME),
                &completion_queue_, callback);
    } else {
        promise->set_value(absl::CancelledError("Channel is gone."));
        delete callback;
    }

    return future;
}

void GrpcConnectionMonitor::AsyncWorker() {
    void* tag;
    bool ok;
    while (completion_queue_.Next(&tag, &ok)) {
        auto* callback = static_cast<MonitorCallback*>(tag);
        const absl::MutexLock lock(callback_active_);
        callback->Run(ok);
    }
    VLOG(1) << "Queue is finished.";
}

void GrpcConnectionMonitor::SetConnectionState(ConnectionState state) {
    state_ = state;
    state_changes.FireEvent(state);
}

}  // namespace android::emulation::control
