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

#include <grpcpp/grpcpp.h>

#include <future>
#include <memory>
#include <thread>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "goldfish/eventing/event_sources.h"

namespace android::emulation::control {

/**
 * @brief Monitors the state of a gRPC channel asynchronously.
 *
 * This class is an internal helper for the `CallbackEmulatorGrpcClient`. It
 * encapsulates the `grpc::CompletionQueue` and its worker thread, providing a
 * clean, future-based API for connection state monitoring. It is responsible
 * for watching the channel's connectivity state and reporting changes through
 * the `state_changes` event source.
 */
class GrpcConnectionMonitor {
  public:
    /**
     * @brief Constructs a new GrpcConnectionMonitor.
     * @param channel The gRPC channel to monitor.
     */
    explicit GrpcConnectionMonitor(const std::shared_ptr<grpc::Channel>& channel);

    /**
     * @brief Destructor that stops the monitoring thread.
     */
    ~GrpcConnectionMonitor();

    /**
     * @brief Asynchronously waits for the channel to enter the `READY` state.
     *
     * This method initiates the monitoring process on a background thread. It
     * returns a future that will be fulfilled once the connection is established
     * or the timeout is reached.
     *
     * @param timeout The maximum duration to wait for a connection.
     * @return A `std::future<absl::Status>` that will resolve with
     *         `absl::OkStatus()` on a successful connection or an error status
     *         on failure or timeout.
     */
    std::future<absl::Status> Watch(absl::Duration timeout);

    /**
     * @brief Stops the monitoring and disconnects the channel.
     *
     * This method shuts down the completion queue and joins the worker thread,
     * ensuring a clean shutdown.
     */
    void Stop();

    /**
     * @brief An event source that fires when the connection state changes.
     *
     * The `CallbackEmulatorGrpcClient` subscribes to this event to provide
     * real-time connection status updates to its users.
     */
    android::base::eventing::CallbackEventSource<ConnectionState> state_changes;

    /**
     * @brief Checks if the current thread is the monitor's worker thread.
     * @return `true` if the calling thread is the worker thread, `false`
     *         otherwise.
     */
    bool IsWorkerThread() const { return std::this_thread::get_id() == worker_thread_id_; }

  private:
    // Forward declaration of the internal callback handler.
    struct MonitorCallback;

    void AsyncWorker();
    void SetConnectionState(ConnectionState state);

    std::weak_ptr<grpc::Channel> channel_;
    grpc::CompletionQueue completion_queue_;
    std::thread worker_thread_;
    std::thread::id worker_thread_id_;
    std::atomic<bool> shutting_down_{false};
    absl::Mutex callback_active_;
    ConnectionState state_{ConnectionState::kDisconnected};
};

}  // namespace android::emulation::control
