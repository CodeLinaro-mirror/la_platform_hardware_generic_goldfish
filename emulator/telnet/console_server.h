// Copyright (C) 2026 The Android Open Source Project
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
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/network/endpoint.h"
#include "line_command_handler.h"

namespace goldfish::telnet {

using ::goldfish::async::AsyncSocket;
using ::goldfish::async::AsyncSocketFactory;
using ::goldfish::async::AsyncSocketServer;
using ::goldfish::async::EventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::network::Endpoint;

/**
 * @class ConsoleServer
 * @brief Manages a telnet-like command server for the emulator.
 *
 * This class binds to a network endpoint and listens for incoming client
 * connections. It employs a thread-per-connection model, where each incoming
 * session is handled by its own dedicated event loop running in a separate
 * background thread.
 *
 * For every connection, it provides a dedicated line-based CommandHandler
 * environment enabling remote control/interrogation of emulator components
 * without blocking the main event loop.
 *
 * It inherits from std::enable_shared_from_this to ensure memory-safety
 * and proper object anchoring during asynchronous background operations.
 */
class ConsoleServer : public std::enable_shared_from_this<ConsoleServer> {
  public:
    friend class ConsoleServerPeer;

    /**
     * @brief Constructs a ConsoleServer.
     *
     * @param factory The socket factory used to create listeners and accept clients.
     * @param main_loop The core thread event loop driving the listener socket.
     * @param endpoint The network endpoint (IP/port) to bind the server to.
     * @param handler The command handler orchestrating RPC evaluations.
     * @param context_factory Factory producing thread-local context instances per session.
     */
    ConsoleServer(AsyncSocketFactory& factory, EventLoop* main_loop, Endpoint endpoint,
                  std::shared_ptr<LineCommandHandler> handler);

    /**
     * @brief Destroys the ConsoleServer.
     *
     * Makes a best-effort attempt to stop the server and clean up active
     * connections by calling Stop() with a short timeout. This is a fallback
     * and does not guarantee graceful shutdown. For a clean shutdown,
     * explicitly call Stop() and wait for it to complete before destroying
     * this object.
     */
    ~ConsoleServer();

    /**
     * @brief Starts the listening socket server.
     *
     * @return absl::Status OK if started successfully, AlreadyExistsError if
     *                        already running, or InternalError on bind failure.
     */
    absl::Status Start();

    /**
     * @brief Stops and tears down the listening server and active sessions.
     *
     * Unbinds the listener socket to reject new visitors. Then iterates over
     * all active client sessions and instructs them to close.
     *
     * @param timeout The duration to wait for sessions to drain gracefully. After
     *                reaching the timeout, any remaining sessions are abandoned
     *                to cleanly terminate off-thread.
     *
     * @return absl::Status OK if stopped gracefully, or DeadlineExceededError if
     *                        timeout reached waiting for connections.
     */
    absl::Status Stop(absl::Duration timeout = absl::InfiniteDuration());

  private:
    class Connection;

    /// @struct PendingLoopInfo
    /// @brief Stashes connection context (loop and endpoint) during the async handshake.
    struct PendingLoopInfo {
        std::unique_ptr<ThreadedEventLoop> loop;
        Endpoint remote;
    };

    /// @struct Session
    /// @brief Represents an active client session, owning both the connection state and its thread.
    struct Session {
        std::shared_ptr<Connection> connection;
        std::unique_ptr<ThreadedEventLoop> loop;
    };

    using PendingLoops = absl::flat_hash_map<EventLoop*, PendingLoopInfo>;
    using ActiveSessions = absl::flat_hash_map<uint64_t, Session>;

    EventLoop* ProvideLoopForConnection(Endpoint remote);
    bool OnClientConnected(std::shared_ptr<AsyncSocket> socket);
    void RequestTeardown(uint64_t id);
    void RemoveConnection(uint64_t id);
    void StopActiveConnections();

    AsyncSocketFactory& factory_;  ///< Factory used to create sockets and servers.
    EventLoop* main_loop_;         ///< Main thread event loop managing the listener.
    Endpoint endpoint_;            ///< Network endpoint where the server is bound.
    const std::shared_ptr<LineCommandHandler>
            handler_;  ///< Command handler to evaluate incoming requests.

    absl::Mutex mutex_;
    std::shared_ptr<AsyncSocketServer> server_
            ABSL_GUARDED_BY(mutex_);  ///< Underlying server socket instance.

    std::atomic<uint64_t> next_session_id_{1};  ///< Rolling counter generating unique session IDs.

    PendingLoops pending_loops_
            ABSL_GUARDED_BY(mutex_);  ///< Stashes per-connection threaded loops before handshake.
    ActiveSessions active_sessions_
            ABSL_GUARDED_BY(mutex_);  ///< Active client connections indexed by session ID.
};

}  // namespace goldfish::telnet
