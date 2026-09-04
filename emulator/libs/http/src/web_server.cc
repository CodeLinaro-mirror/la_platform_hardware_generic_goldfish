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
#include "goldfish/http/web_server.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/scoped_async_resource.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/http/http_session.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::http {

namespace {

template <typename F>
absl::Status PostAndWaitOrRun(async::EventLoop* loop, F&& fn) {
    if (loop != nullptr && !loop->IsOnLoopThread()) {
        if (loop->GetState() == async::LooperStatusEvent::State::kRunning) {
            auto result = loop->PostAndWait(std::forward<F>(fn));
            if (!result.ok()) {
                return result.status();
            }
            return *result;
        }
        return absl::FailedPreconditionError("EventLoop is not in running state");
    }
    return fn();
}

}  // namespace

CreateWebServer::CreateWebServer(uint16_t port) : port_(port) {}

CreateWebServer& CreateWebServer::Port(uint16_t p) {
    port_ = p;
    return *this;
}

CreateWebServer& CreateWebServer::BindAddress(std::string address) {
    address_ = std::move(address);
    return *this;
}

CreateWebServer& CreateWebServer::MaxPayloadSize(size_t bytes) {
    max_payload_size_ = bytes;
    return *this;
}

CreateWebServer& CreateWebServer::EventLoop(async::EventLoop* loop) {
    loop_ = loop;
    return *this;
}

CreateWebServer& CreateWebServer::SocketFactory(
        std::shared_ptr<async::AsyncSocketFactory> factory) {
    socket_factory_ = std::move(factory);
    return *this;
}

CreateWebServer& CreateWebServer::AccessLog(AccessLogger logger) {
    access_logger_ = std::move(logger);
    return *this;
}

absl::Status CreateWebServer::Validate() const {
    if (max_payload_size_ == 0) {
        return absl::InvalidArgumentError("MaxPayloadSize must be greater than 0");
    }
    auto ip_status = network::ToIpAddress(address_);
    if (!ip_status.ok()) {
        return absl::InvalidArgumentError(absl::StrCat("Invalid BindAddress '", address_,
                                                       "': ", ip_status.status().message()));
    }
    return absl::OkStatus();
}

struct WebServer::State : public std::enable_shared_from_this<State> {
    CreateWebServer config;
    HttpRouter router;
    HttpHandler not_found_handler;

    async::EventLoop* loop = nullptr;
    std::unique_ptr<async::ThreadedEventLoop> owned_loop;
    std::shared_ptr<async::AsyncSocketFactory> socket_factory;
    async::ScopedAsyncServer server;
    network::Endpoint bound_endpoint;

    absl::Mutex sessions_mutex;
    absl::flat_hash_set<std::shared_ptr<HttpSession>> active_sessions
            ABSL_GUARDED_BY(sessions_mutex);
    absl::Notification shutdown_notification;

    bool OnClientConnected(std::shared_ptr<async::AsyncSocket> socket);
    void HandleSocketRead(const std::shared_ptr<async::AsyncSocket>& sock,
                          const std::shared_ptr<HttpSession>& session, std::string_view data,
                          absl::Status err);
    void TerminateSession(const std::shared_ptr<async::AsyncSocket>& sock,
                          const std::shared_ptr<HttpSession>& session);
    void RemoveSession(const std::shared_ptr<HttpSession>& session);
    void Stop();
};

WebServer::WebServer(CreateWebServer config) : state_(std::make_shared<State>()) {
    state_->config = std::move(config);
}

WebServer::~WebServer() {
    Stop();
}

WebServer& WebServer::OnGet(std::string path, HttpHandler handler) {
    state_->router.AddRoute(HttpMethod::kGet, std::move(path), std::move(handler));
    return *this;
}

WebServer& WebServer::OnPost(std::string path, HttpHandler handler) {
    state_->router.AddRoute(HttpMethod::kPost, std::move(path), std::move(handler));
    return *this;
}

WebServer& WebServer::OnPut(std::string path, HttpHandler handler) {
    state_->router.AddRoute(HttpMethod::kPut, std::move(path), std::move(handler));
    return *this;
}

WebServer& WebServer::OnDelete(std::string path, HttpHandler handler) {
    state_->router.AddRoute(HttpMethod::kDelete, std::move(path), std::move(handler));
    return *this;
}

WebServer& WebServer::OnOptions(std::string path, HttpHandler handler) {
    state_->router.AddRoute(HttpMethod::kOptions, std::move(path), std::move(handler));
    return *this;
}

WebServer& WebServer::OnStream(HttpMethod method, std::string path, AsyncHttpHandler handler) {
    state_->router.AddStreamingRoute(method, std::move(path), std::move(handler));
    return *this;
}

void WebServer::SetNotFoundHandler(HttpHandler handler) {
    state_->not_found_handler = std::move(handler);
}

absl::Status WebServer::Start(bool blocking) {
    if (state_->server) {
        return absl::AlreadyExistsError("WebServer is already running");
    }

    auto valid_status = state_->config.Validate();
    if (!valid_status.ok()) {
        return valid_status;
    }

    if (state_->config.EventLoop() != nullptr) {
        state_->loop = state_->config.EventLoop();
    } else {
        auto uv_loop = async::LibuvEventLoop::Create("WebServerLoop");
        state_->owned_loop = async::ThreadedEventLoop::Create(std::move(uv_loop));
        if (!state_->owned_loop) {
            return absl::InternalError("Failed to initialize ThreadedEventLoop");
        }
        state_->loop = state_->owned_loop.get();
    }

    if (state_->config.SocketFactory() != nullptr) {
        state_->socket_factory = state_->config.SocketFactory();
    } else {
        state_->socket_factory = std::make_shared<async::LibuvAsyncSocketFactory>();
    }

    auto ip_opt = network::ToIpAddress(state_->config.BindAddress());
    if (!ip_opt.ok()) {
        return ip_opt.status();
    }
    network::Endpoint endpoint = network::ToEndpoint(*ip_opt, state_->config.Port());

    auto create_fn = [state = state_, endpoint]() -> absl::Status {
        auto on_connect = [weak_state = std::weak_ptr<State>(state)](
                                  std::shared_ptr<async::AsyncSocket> socket) -> bool {
            if (auto s = weak_state.lock()) {
                return s->OnClientConnected(std::move(socket));
            }
            return false;
        };

        state->server = async::ScopedAsyncServer(
                state->socket_factory->CreateServer(state->loop, endpoint, on_connect));
        if (!state->server) {
            return absl::InternalError("Failed to bind and listen on HTTP endpoint");
        }
        state->bound_endpoint = state->server->GetEndpoint();
        return absl::OkStatus();
    };

    auto status = PostAndWaitOrRun(state_->loop, create_fn);
    if (!status.ok()) {
        return status;
    }

    if (blocking) {
        state_->shutdown_notification.WaitForNotification();
    }

    return absl::OkStatus();
}

void WebServer::Stop() {
    if (state_) {
        state_->Stop();
    }
}

network::Endpoint WebServer::GetEndpoint() const {
    if (!state_) {
        return {};
    }
    return state_->bound_endpoint;
}

size_t WebServer::ActiveSessionsCount() const {
    if (!state_) {
        return 0;
    }
    const absl::MutexLock lock(&state_->sessions_mutex);
    return state_->active_sessions.size();
}

void WebServer::State::RemoveSession(const std::shared_ptr<HttpSession>& session) {
    const absl::MutexLock lock(&sessions_mutex);
    active_sessions.erase(session);
}

void WebServer::State::Stop() {
    auto cleanup_fn = [this]() {
        if (server) {
            server->Close();
            server = async::ScopedAsyncServer();
        }
        absl::flat_hash_set<std::shared_ptr<HttpSession>> sessions;
        {
            const absl::MutexLock lock(&sessions_mutex);
            sessions = std::exchange(active_sessions, {});
        }
        for (const auto& session : sessions) {
            session->DetachSocket();
        }
    };

    if (loop != nullptr && !loop->IsOnLoopThread()) {
        if (loop->GetState() == async::LooperStatusEvent::State::kRunning) {
            loop->PostAndWait(cleanup_fn).IgnoreError();
        } else {
            cleanup_fn();
        }
    } else {
        cleanup_fn();
    }

    if (!shutdown_notification.HasBeenNotified()) {
        shutdown_notification.Notify();
    }
}

void WebServer::State::TerminateSession(const std::shared_ptr<async::AsyncSocket>& sock,
                                        const std::shared_ptr<HttpSession>& session) {
    session->DetachSocket();
    if (sock) {
        sock->Close();
    }
    RemoveSession(session);
}

void WebServer::State::HandleSocketRead(const std::shared_ptr<async::AsyncSocket>& sock,
                                        const std::shared_ptr<HttpSession>& session,
                                        std::string_view data, absl::Status err) {
    if (!sock) {
        return;
    }
    if (!err.ok()) {
        TerminateSession(sock, session);
        return;
    }
    if (!session->OnDataReceived(data)) {
        TerminateSession(sock, session);
    }
}

bool WebServer::State::OnClientConnected(std::shared_ptr<async::AsyncSocket> socket) {
    if (!socket) {
        return false;
    }

    auto session = HttpSession::Create(socket, loop, &router, config.MaxPayloadSize(),
                                       not_found_handler, config.AccessLog());
    {
        const absl::MutexLock lock(&sessions_mutex);
        active_sessions.insert(session);
    }

    auto weak_self = weak_from_this();

    session->SetOnDetachedCallback([weak_self](const std::shared_ptr<HttpSession>& s) {
        if (auto self = weak_self.lock()) {
            self->RemoveSession(s);
        }
    });

    socket->SetOnReadCallbackNoFlowControl(
            [weak_self, weak_session = std::weak_ptr<HttpSession>(session),
             weak_socket = std::weak_ptr<async::AsyncSocket>(socket)](std::string_view data,
                                                                      absl::Status err) {
                if (auto self = weak_self.lock()) {
                    if (auto sess = weak_session.lock()) {
                        self->HandleSocketRead(weak_socket.lock(), sess, data, err);
                    }
                }
            });

    socket->SetOnCloseCallback([weak_self, weak_session = std::weak_ptr<HttpSession>(session),
                                weak_socket = std::weak_ptr<async::AsyncSocket>(socket)]() {
        if (auto self = weak_self.lock()) {
            if (auto sess = weak_session.lock()) {
                self->TerminateSession(weak_socket.lock(), sess);
            }
        }
    });

    return true;
}

}  // namespace goldfish::http
