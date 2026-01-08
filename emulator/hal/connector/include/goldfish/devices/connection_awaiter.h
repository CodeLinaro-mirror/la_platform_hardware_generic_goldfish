// Copyright 2024 The Android Open Source Project
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
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {

using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;
namespace async = goldfish::async;

/**
 * @brief A class that facilitates asynchronous connection establishment with retry logic.
 *
 * `ConnectionAwaiter` simplifies the process of connecting to a remote endpoint by
 * repeatedly attempting to create a connection until it succeeds. It provides a
 * callback mechanism to notify the user when the connection is successfully established.
 */
class ConnectionAwaiter : public IPlug, public std::enable_shared_from_this<ConnectionAwaiter> {
    struct Private {};

  public:
    /**
     * @brief A function type representing a connection creation attempt.
     *
     * This function takes a `PlugPtr` as input and returns a `SocketPtr` representing
     * the established connection or nullptr if the connection attempt fails.
     */
    using CreateConnection = std::function<SocketPtr(PlugPtr)>;

    /**
     * @brief A function type representing a callback invoked upon successful connection.
     *
     * This function takes a `SocketPtr` as input, representing the established connection.
     */
    using ConnectionCallback = std::function<void(SocketPtr)>;

    /**
     * @brief Constructor. Initializes the `ConnectionAwaiter` and starts the retry task.
     *
     * @param eventLoop The event loop instance for scheduling tasks.
     * @param createConnection The function to create a connection.
     * @param onConnected The callback to invoke upon successful connection.
     * @param interval The retry interval.
     */
    ConnectionAwaiter(async::EventLoop* eventLoop, CreateConnection createConnection,
                      ConnectionCallback onConnected, std::chrono::milliseconds interval, Private);

    /**
     * @brief Destructor. Stops the connection retry task.
     */
    ~ConnectionAwaiter();

    /**
     * @brief Handles successful connection establishment.
     *
     * This method is called when the connection is successfully established.
     */
    void OnConnect() override;

    /**
     * This method is not expected to be called in the normal operation of this class,
     * as the socket is typically handed off to another component upon successful connection.
     */
    bool OnReceive(const void* data, size_t size) override;

    /**
     * This method is not expected to be called under normal circumstances, as the socket is
     * typically handed off after a successful connection.
     */
    SocketPtr OnUnplug() override;

    /**
     * @brief Continuously attempts to establish a connection until successful.
     *
     * This method initiates a repeated process of attempting to create a connection
     * using the provided `createConnection` factory function. It will continue to
     * invoke this function at regular intervals specified by `interval` until the
     * connection is successfully established. Once the connection is established,
     * the `onConnected` callback will be invoked.
     *
     * @param eventLoop The event loop instance responsible for scheduling connection retry tasks.
     * @param createConnection A factory function that attempts to create a connection.
     *                         This will be called repeatedly until a connection is made.
     * @param onConnected A callback function that will be triggered once the connection
     *                    is successfully established.
     * @param interval The time interval between successive connection attempts.
     *
     * @return std::shared_ptr<ConnectionAwaiter> A shared pointer to the `ConnectionAwaiter`
     *                                            object that manages the retry process.
     */
    static std::shared_ptr<ConnectionAwaiter> retryUntilConnected(
            async::EventLoop* eventLoop, CreateConnection createConnection,
            ConnectionCallback onConnected, std::chrono::milliseconds interval);

  private:
    /**
     * @brief Attempts to establish a connection.
     *
     * This method attempts to create a connection using the `mCreateConnection` function.
     * It returns true if a connection attempt was made, and false if the connection
     * is already established.
     *
     * @return True if a connection attempt was made, false otherwise.
     */
    bool attemptConnection();

    /// Flag indicating whether a connection is established.
    bool mIsConnected{false};

    /// The established socket connection (if any).
    SocketPtr mSocket{nullptr};

    /// Mutex to protect connection-related operations
    std::mutex mConnectionMutex;

    /// Function to create a connection
    CreateConnection mCreateConnection;

    /// Callback invoked upon successful connection
    ConnectionCallback mOnConnected;

    /// Task responsible for retrying connection attempts
    std::shared_ptr<async::EventLoop::Timer> mConnectionRetryTask;
};
}  // namespace devices
}  // namespace goldfish