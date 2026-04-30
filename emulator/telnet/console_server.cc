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

#include "console_server.h"

#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"

#include "goldfish/async/libuv_event_loop.h"

namespace goldfish::telnet {

class ConsoleServer::Connection : public std::enable_shared_from_this<ConsoleServer::Connection> {
  public:
    Connection(std::shared_ptr<AsyncSocket> socket, std::shared_ptr<LineCommandHandler> handler,
               std::unique_ptr<LineCommandHandler::Context> context,
               std::weak_ptr<ConsoleServer> server, uint64_t session_id, Endpoint remote)
            : socket_(std::move(socket))
            , handler_(std::move(handler))
            , context_(std::move(context))
            , server_(std::move(server))
            , session_id_(session_id)
            , remote_(std::move(remote)) {}

    void Start() {
        socket_->SetOnReadCallbackNoFlowControl(
                [weak_self = std::weak_ptr<Connection>(shared_from_this())](
                        std::string_view data, const absl::Status& err) {
                    auto self = weak_self.lock();
                    if (!self) return;
                    if (!err.ok()) {
                        LOG(ERROR) << "Console session " << self->session_id_ << " from "
                                   << goldfish::network::ToString(self->remote_)
                                   << " encountered a network error: " << err;
                        self->Close();
                        return;
                    }
                    self->OnData(data);
                });

        socket_->SetOnCloseCallback([weak_self = std::weak_ptr<Connection>(shared_from_this())]() {
            if (auto self = weak_self.lock()) {
                VLOG(1) << "Console session " << self->session_id_ << " from "
                        << goldfish::network::ToString(self->remote_) << " has been closed.";
                if (auto s = self->server_.lock()) {
                    s->RequestTeardown(self->session_id_);
                }
            }
        });

        // Send welcome message after setting the read callback.
        const std::string welcome = handler_->WelcomeMessage(*context_);
        if (!welcome.empty()) {
            SendLine(welcome);
        }
    }

    void Close() {
        if (closed_.exchange(true)) {
            return;  // Already closing
        }

        VLOG(1) << "Closing console session " << session_id_ << " for "
                << goldfish::network::ToString(remote_);
        if (socket_) {
            socket_->GetLoop()->Post([s = socket_]() { s->Close(); }).IgnoreError();
        }
    }

    void Stop() { Close(); }

  private:
    void SendLine(const std::string& line) {
        auto msg = absl::StrCat(line, "\r\n");
        VLOG(1) << "Send: " << absl::CEscape(msg);
        socket_->Send(msg.data(), msg.size()).IgnoreError();
    }

    void HandleLine(const std::string& line) {
        VLOG(1) << "Recv: " << absl::CEscape(line);
        auto result = (*handler_)(line, *context_);

        if (absl::IsAborted(result.status())) {
            VLOG(1) << "Console session " << session_id_ << " aborted command execution.";
            socket_->Close();
            return;
        }

        if (!result.ok()) {
            SendLine(absl::StrCat("KO: ", result.status().message()));
            return;
        }

        std::string response = result->empty() ? "OK" : absl::StrCat(*result, "\r\nOK");
        SendLine(response);
    }

    void OnData(std::string_view data) {
        VLOG(2) << "Raw data: " << absl::CEscape(data);
        buffer_.append(data);
        size_t start = 0;
        size_t pos;
        while ((pos = buffer_.find('\n', start)) != std::string::npos) {
            std::string line = buffer_.substr(start, pos - start);

            // Remove trailing carriage return if present.
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            start = pos + 1;
            HandleLine(line);
        }
        buffer_.erase(0, start);
    }

    std::shared_ptr<AsyncSocket> socket_;
    std::shared_ptr<LineCommandHandler> handler_;
    std::unique_ptr<LineCommandHandler::Context> context_;
    std::weak_ptr<ConsoleServer> server_;
    uint64_t session_id_;
    Endpoint remote_;
    std::string buffer_;
    std::atomic<bool> closed_{false};
};

ConsoleServer::ConsoleServer(AsyncSocketFactory& factory, EventLoop* main_loop, Endpoint endpoint,
                             std::shared_ptr<LineCommandHandler> handler)
        : factory_(factory)
        , main_loop_(main_loop)
        , endpoint_(std::move(endpoint))
        , handler_(std::move(handler)) {}

absl::Status ConsoleServer::Start() {
    LOG(INFO) << "Starting emulator console server on " << goldfish::network::ToString(endpoint_);
    {
        const absl::MutexLock lock(mutex_);
        if (server_) {
            VLOG(1) << "Attempted to start the console server, but it is already running.";
            return absl::AlreadyExistsError("Server already started");
        }
    }

    auto server = factory_.CreateServer(
            main_loop_, endpoint_,
            [this](std::shared_ptr<AsyncSocket> socket) {
                return OnClientConnected(std::move(socket));
            },
            [this](Endpoint remote) { return ProvideLoopForConnection(std::move(remote)); });

    if (!server) {
        return absl::InternalError("Failed to bind server");
    }

    {
        const absl::MutexLock lock(mutex_);
        if (server_) {
            return absl::AlreadyExistsError("Server already started concurrently");
        }
        server_ = std::move(server);
        // Update the port to the actual port the server is listening on. This is
        // necessary because the endpoint is set to port 0, which means the
        // operating system will assign an ephemeral port.
        endpoint_ = server_->GetEndpoint();
    }
    return absl::OkStatus();
}

ConsoleServer::~ConsoleServer() {
    // Last ditch effort to clean up connections if needed.
    Stop(absl::Milliseconds(100)).IgnoreError();
}

absl::Status ConsoleServer::Stop(absl::Duration timeout) {
    LOG(INFO) << "Stopping console server (timeout: " << absl::FormatDuration(timeout) << ").";
    std::shared_ptr<AsyncSocketServer> server_to_close;
    {
        const absl::MutexLock lock(mutex_);
        if (server_) {
            server_to_close = std::move(server_);
        }
    }

    if (server_to_close) {
        VLOG(1) << "Closing console server listening socket.";
        if (main_loop_->IsOnLoopThread()) {
            server_to_close->Close();
        } else {
            main_loop_
                    ->PostAndWait([local_server = std::move(server_to_close)]() mutable {
                        local_server->Close();
                        // Explicitly reset on the loop thread, vs. letting the
                        // closure destroy the object on the caller thread.
                        local_server.reset();
                    })
                    .IgnoreError();
        }
    }

    // Do not early return if server_to_close is null; concurrent Stop() calls must still wait for
    // active_sessions_ to drain.
    {
        const absl::MutexLock lock(mutex_);
        pending_loops_.clear();
    }

    StopActiveConnections();

    if (main_loop_->IsOnLoopThread()) {
        VLOG(1) << "Console server stop triggered from main thread; bypassing wait. "
                   "Remaining sessions will gracefully terminate asynchronously.";
        return absl::OkStatus();
    }

    const absl::MutexLock lock(mutex_);
    VLOG(1) << "Waiting for " << active_sessions_.size()
            << " active connections to close gracefully";
    // The mutex is held while this condition is evaluated by absl::Condition.
    // We bypass thread safety analysis because Clang cannot statically trace the lock across the
    // lambda boundary.
    auto condition = +[](ConsoleServer* arg) ABSL_NO_THREAD_SAFETY_ANALYSIS {
        return arg->active_sessions_.empty();
    };
    if (!mutex_.AwaitWithTimeout(absl::Condition(condition, this), timeout)) {
        LOG(WARNING) << "Console server timed out waiting for active connections ("
                     << active_sessions_.size() << " remaining).";
        return absl::DeadlineExceededError(
                "Console server timed out waiting for active connections to close gracefully");
    }
    return absl::OkStatus();
}

EventLoop* ConsoleServer::ProvideLoopForConnection(Endpoint remote) {
    VLOG(2) << "Allocating new thread loop for incoming connection from "
            << goldfish::network::ToString(remote);
    auto libuv_loop = goldfish::async::LibuvEventLoop::Create("ConsoleServerLoop");
    auto thread_loop = ThreadedEventLoop::Create(std::move(libuv_loop));
    auto* loop_ptr = thread_loop.get();

    const absl::MutexLock lock(mutex_);
    auto [it, inserted] = pending_loops_.try_emplace(
            loop_ptr, PendingLoopInfo{.loop = std::move(thread_loop), .remote = std::move(remote)});
    DCHECK(inserted) << "Duplicate loop pointer in pending_loops_: " << loop_ptr;
    return loop_ptr;
}

bool ConsoleServer::OnClientConnected(std::shared_ptr<AsyncSocket> socket) {
    std::unique_ptr<ThreadedEventLoop> thread_loop;
    Endpoint remote;
    {
        const absl::MutexLock lock(mutex_);
        auto* loop = socket->GetLoop();
        auto it = pending_loops_.find(loop);
        if (it == pending_loops_.end()) {
            // Concurrency corner case where we are shutting down while half way
            // in a connect. i.e. we handed out a loop and someone stopped us.
            return false;
        }

        thread_loop = std::move(it->second.loop);
        remote = std::move(it->second.remote);
        pending_loops_.erase(it);
    }

    const uint64_t session_id = next_session_id_.fetch_add(1, std::memory_order_relaxed);
    LOG(INFO) << "New console session " << session_id << " established from "
              << goldfish::network::ToString(remote);

    auto context = handler_->CreateContext();
    auto conn = std::make_shared<Connection>(std::move(socket), handler_, std::move(context),
                                             weak_from_this(), session_id, std::move(remote));

    {
        const absl::MutexLock lock(mutex_);
        auto [it, inserted] = active_sessions_.try_emplace(
                session_id, Session{.connection = conn, .loop = std::move(thread_loop)});
        DCHECK(inserted) << "Duplicate session ID in active_sessions_: " << session_id;
    }

    conn->Start();
    return true;
}

void ConsoleServer::StopActiveConnections() {
    std::vector<std::shared_ptr<Connection>> conns;
    {
        const absl::MutexLock lock(mutex_);
        if (active_sessions_.empty()) return;
        VLOG(1) << "Stopping " << active_sessions_.size() << " active console connections.";
        for (const auto& [id, session] : active_sessions_) {
            // We cannot move the connection as StopActiveConnections can be called multiple times
            // (e.g. multiple Stop() calls, or Stop() followed by dtor), so we collect shared_ptrs
            // to all connections first, then call Close on them without holding the lock.
            conns.push_back(session.connection);
        }
    }

    // Next we are going to request all connections to stop.
    // This will trigger the connection's Close method, which will in turn trigger the
    // OnClose callback that removes the connection from active_sessions_.
    for (auto& conn : conns) {
        conn->Close();
    }
}

void ConsoleServer::RequestTeardown(uint64_t id) {
    DCHECK(!main_loop_->IsOnLoopThread())
            << "This should be called from a connection thread, not the main loop thread.";
    main_loop_->Post([self = shared_from_this(), id]() { self->RemoveConnection(id); })
            .IgnoreError();
}

void ConsoleServer::RemoveConnection(uint64_t id) {
    DCHECK(main_loop_->IsOnLoopThread())
            << "RemoveConnection must be called on the main loop thread, otherwise we risk "
               "deadlocking on ThreadedEventLoop destruction";

    const absl::MutexLock lock(mutex_);
    active_sessions_.erase(id);
}

}  // namespace goldfish::telnet
