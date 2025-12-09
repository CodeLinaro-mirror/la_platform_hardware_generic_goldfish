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

#include "emulator/grpc/client/grpc_connection_monitor.h"

#include <grpcpp/grpcpp.h>

#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace android {
namespace emulation {
namespace control {

inline std::ostream& operator<<(std::ostream& os, ConnectionState state) {
    switch (state) {
    case ConnectionState::Disconnected:
        os << "Disconnected";
        break;
    case ConnectionState::Connecting:
        os << "Connecting";
        break;
    case ConnectionState::Connected:
        os << "Connected";
        break;
    }
    return os;
}

// A self-owning callback that monitors the gRPC channel's state.
// It uses a state machine to transition between initial connection,
// liveness monitoring, and shutdown.
struct GrpcConnectionMonitor::MonitorCallback {
    enum class State { Initial, Monitoring, Shutdown };

    State state = State::Initial;
    GrpcConnectionMonitor* monitor;
    std::shared_ptr<std::promise<absl::Status>> promise;
    absl::Duration timeout;

    MonitorCallback(GrpcConnectionMonitor* m, std::shared_ptr<std::promise<absl::Status>> p,
                    absl::Duration t)
            : monitor(m), promise(std::move(p)), timeout(t) {
        VLOG(1) << "Monitor callback created, with promise";
    }

    void run(bool ok) {
        VLOG(1) << "Running monitor callback";
        auto channel = monitor->mChannel.lock();
        if (!channel || monitor->mShuttingDown.load(std::memory_order_acquire)) {
            VLOG(1) << "Shutting down monitor";
            if (promise) {
                VLOG(1) << "Setting cancel promise";
                promise->set_value(absl::CancelledError("Connection cancelled."));
            }
            delete this;
            return;
        }

        grpc_connectivity_state new_state = channel->GetState(state == State::Initial);

        switch (state) {
        case State::Initial:
            handleInitial(ok, new_state);
            break;
        case State::Monitoring:
            handleMonitoring(ok, new_state);
            break;
        case State::Shutdown:
            delete this;
            break;
        }
    }

    void handleInitial(bool ok, grpc_connectivity_state new_state) {
        auto channel = monitor->mChannel.lock();
        if (!channel || monitor->mShuttingDown.load(std::memory_order_acquire)) {
            delete this;
            return;
        }
        if (new_state == GRPC_CHANNEL_READY) {
            monitor->setConnectionState(ConnectionState::Connected);
            if (promise) {
                VLOG(1) << "Setting ok promise, monitor state: "
                        << (monitor->mShuttingDown ? "shutting down" : "active");
                promise->set_value(absl::OkStatus());
                promise.reset();  // Fulfill promise only once.
            }
            // Transition to long-term monitoring.
            state = State::Monitoring;
            channel->NotifyOnStateChange(new_state, gpr_inf_future(GPR_CLOCK_REALTIME),
                                         &monitor->mCompletionQueue, this);
        } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                   new_state == GRPC_CHANNEL_SHUTDOWN) {
            monitor->setConnectionState(ConnectionState::Disconnected);
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
                    &monitor->mCompletionQueue, this);
        }
    }

    void handleMonitoring(bool ok, grpc_connectivity_state new_state) {
        auto channel = monitor->mChannel.lock();
        if (!channel || monitor->mShuttingDown.load(std::memory_order_acquire)) {
            delete this;
            return;
        }
        if (!ok || new_state != GRPC_CHANNEL_READY) {
            monitor->setConnectionState(ConnectionState::Disconnected);
            // End of the line for this monitor.
            delete this;
        } else {
            // Spurious notification, re-arm the monitor.
            channel->NotifyOnStateChange(GRPC_CHANNEL_READY, gpr_inf_future(GPR_CLOCK_REALTIME),
                                         &monitor->mCompletionQueue, this);
        }
    }
};

GrpcConnectionMonitor::GrpcConnectionMonitor(std::shared_ptr<grpc::Channel> channel)
        : mChannel(channel) {
    mWorkerThread = std::thread(&GrpcConnectionMonitor::asyncWorker, this);
    mWorkerThreadId = mWorkerThread.get_id();
}

GrpcConnectionMonitor::~GrpcConnectionMonitor() {
    assert(std::this_thread::get_id() != mWorkerThreadId &&
           "The monitor thread should not be destroying the monitor!");
    stop();
}

void GrpcConnectionMonitor::stop() {
    if (std::this_thread::get_id() == mWorkerThreadId) {
        VLOG(1) << "You cannot stop the monitor thread from the monitor thread.";
        return;
    }

    if (mShuttingDown.exchange(true)) {
        return;
    }
    VLOG(1) << "mShuttingDown set to: " << mShuttingDown;
    if (mWorkerThread.joinable()) {
        VLOG(1) << "Joining worker thread, we are disconnecting";
        {
            // Do not yank the queue out when a callback is active, callbacks could schedule
            // something which is not allowed after this call returns.
            absl::MutexLock lock(&mCallbackActive);
            mCompletionQueue.Shutdown();
        }
        mWorkerThread.join();
    }
}

std::future<absl::Status> GrpcConnectionMonitor::watch(absl::Duration timeout) {
    auto promise = std::make_shared<std::promise<absl::Status>>();
    auto future = promise->get_future();

    auto* callback = new MonitorCallback(this, promise, timeout);
    if (auto channel = mChannel.lock()) {
        channel->NotifyOnStateChange(
                channel->GetState(false),
                gpr_time_from_millis(absl::ToInt64Milliseconds(timeout), GPR_CLOCK_REALTIME),
                &mCompletionQueue, callback);
    } else {
        promise->set_value(absl::CancelledError("Channel is gone."));
        delete callback;
    }

    return future;
}

void GrpcConnectionMonitor::asyncWorker() {
    void* tag;
    bool ok;
    while (mCompletionQueue.Next(&tag, &ok)) {
        auto* callback = static_cast<MonitorCallback*>(tag);
        absl::MutexLock lock(&mCallbackActive);
        callback->run(ok);
    }
    VLOG(1) << "Queue is finished.";
}

void GrpcConnectionMonitor::setConnectionState(ConnectionState state) {
    mState = state;
    mStateChanges.fireEvent(state);
}

}  // namespace control
}  // namespace emulation
}  // namespace android