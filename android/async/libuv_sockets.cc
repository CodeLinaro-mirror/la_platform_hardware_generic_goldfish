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
#include <uv.h>

#include "goldfish/async/libuv_socket_factory.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/uv_to_absl.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {

using goldfish::network::Endpoint;
using goldfish::network::ToEndpoint;

namespace {

struct WriteReqT {
    uv_write_t req;
    uv_buf_t buf;
    AsyncSocket::OnSendCallback cb;

    static WriteReqT* Create(const char* buffer_data, size_t buffer_size,
                             AsyncSocket::OnSendCallback cb) {
        const size_t total_size = sizeof(WriteReqT) + buffer_size;
        void* raw_memory = malloc(total_size);
        DCHECK(raw_memory) << "Ran out of memory while creating packet";
        auto* write_req = new (raw_memory) WriteReqT();
        char* write_buffer = reinterpret_cast<char*>(write_req) + sizeof(WriteReqT);
        memcpy(write_buffer, buffer_data, buffer_size);

        write_req->buf = uv_buf_init(write_buffer, buffer_size);
        write_req->cb = std::move(cb);

        return write_req;
    }

    static void Destroy(WriteReqT* w) {
        w->~WriteReqT();
        free(w);
    }
};

// =================================================================
//                 CONCRETE IMPLEMENTATION CLASSES
// =================================================================

class LibuvSocket : public AsyncSocket, public std::enable_shared_from_this<LibuvSocket> {
  public:
    explicit LibuvSocket(EventLoop* loop) : LibuvSocket(loop, {}, /*is_incoming=*/true) {}

    LibuvSocket(EventLoop* loop, Endpoint endpoint)
            : LibuvSocket(loop, std::move(endpoint), /*is_incoming=*/false) {}

    ~LibuvSocket() override {
        DCHECK(uv_is_closing((const uv_handle_t*)&tcp_handle_))
                << "LibuvSocket destroyed without calling close() first!";
    }

    // --- Configuration Methods ---
    void SetOnReadCallbackNoFlowControl(OnReadCallback cb) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_read_ = std::move(cb);
    }

    void OnFlowControlEvent(const bool enable_reading) override {
        auto weak_self = std::weak_ptr<LibuvSocket>(shared_from_this());
        event_loop_->Post([enable_reading, weak_self = std::move(weak_self)]() {
            if (const auto self = weak_self.lock()) {
                if (enable_reading) {
                    self->StartReading();
                } else {
                    uv_read_stop(reinterpret_cast<uv_stream_t*>(&(self->tcp_handle_)));
                }
            }
        });
    }

    void SetOnCloseCallback(OnCloseCallback cb) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_close_ = std::move(cb);
    }

    void SetOnConnectedCallback(OnConnectCallback cb) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_connected_ = std::move(cb);
    }

    // --- I/O Methods ---
    absl::Status Send(const char* buffer, size_t buffer_size, OnSendCallback cb) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "buffer_sizelled on loop thread";

        if (!is_connected_ || uv_is_closing(reinterpret_cast<const uv_handle_t*>(&tcp_handle_))) {
            return UvErrToAbslStatus(UV_ENOTCONN);
        }

        auto* write_req = WriteReqT::Create(buffer, buffer_size, std::move(cb));

        uv_write(&write_req->req, reinterpret_cast<uv_stream_t*>(&tcp_handle_), &write_req->buf, 1,
                 [](uv_write_t* req, int s) {
                     auto* w = reinterpret_cast<WriteReqT*>(req);
                     w->cb(UvErrToAbslStatus(s));
                     WriteReqT::Destroy(w);
                 });
        return absl::OkStatus();
    }

    void Close() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        is_connected_ = false;

        if (!uv_is_closing(reinterpret_cast<const uv_handle_t*>(&tcp_handle_))) {
            tcp_handle_.data = new std::shared_ptr<LibuvSocket>(shared_from_this());
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp_handle_), [](uv_handle_t* h) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(h->data);
                if ((*self_ptr)->on_close_) {
                    (*self_ptr)->on_close_();
                }
                delete self_ptr;
            });
        }
    }

    absl::Status Connect() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        if (is_connected_) {
            return absl::OkStatus();
        }

        auto* connect_req = new uv_connect_t();
        connect_req->data = new std::shared_ptr<LibuvSocket>(shared_from_this());

        struct sockaddr_storage addr = ToSockaddr(endpoint_);
        uv_tcp_connect(connect_req, &tcp_handle_, reinterpret_cast<const struct sockaddr*>(&addr),
                       [](uv_connect_t* req, int s) {
                           auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(req->data);
                           (*self_ptr)->OnConnect(s);
                           delete self_ptr;
                           delete req;
                       });
        return absl::OkStatus();
    }

    bool Connected() const override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        return is_connected_ && !uv_is_closing(reinterpret_cast<const uv_handle_t*>(&tcp_handle_));
    }

    EventLoop* GetLoop() const override { return event_loop_; }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[uvs %s%s %s L:%p]", (is_incoming_ ? "<-" : "->"),
                     (is_connected_ ? "+" : "-"), ToString(endpoint_), GetLoop());
    }

  private:
    template <size_t kMaxAllocs>
    struct ReadBufferAllocator {
        explicit ReadBufferAllocator(uint32_t buf_size) : mbuffer_size(buf_size) {}

        // https://docs.libuv.org/en/v1.x/handle.html#c.uv_alloc_cb
        // A suggested size ... is provided, but it’s just an indication ...
        // The user is free to allocate the amount of memory they decide.
        uv_buf_t Alloc(size_t /*suggestedSize*/) {
            if (allocs_size) {
                --allocs_size;
                std::unique_ptr<char[]> mem = std::move(allocs[allocs_size]);
                return uv_buf_init(mem.release(), mbuffer_size);
            }
            return uv_buf_init(new char[mbuffer_size], mbuffer_size);
        }

        void Free(const uv_buf_t& buf) {
            std::unique_ptr<char[]> mem = std::unique_ptr<char[]>(buf.base);

            if (allocs_size < kMaxAllocs) {
                allocs[allocs_size] = std::move(mem);
                ++allocs_size;
            }
        }

        std::array<std::unique_ptr<char[]>, kMaxAllocs> allocs;
        const uint32_t mbuffer_size;
        uint32_t allocs_size = 0;
    };

    friend class LibuvServer;

    LibuvSocket(EventLoop* loop, Endpoint endpoint, const bool is_incoming)
            : event_loop_(loop)
            , loop_(static_cast<uv_loop_t*>(loop->GetRawLoop()))
            , endpoint_(std::move(endpoint))
            , read_buffer_allocator_(16384)
            , is_incoming_(is_incoming) {
        uv_tcp_init(loop_, &tcp_handle_);
        uv_tcp_nodelay(&tcp_handle_, 1);
        tcp_handle_.data = this;
    }

    void StartReading() {
        DCHECK(event_loop_->IsOnLoopThread());
        if (uv_is_closing(reinterpret_cast<const uv_handle_t*>(&tcp_handle_))) return;

        uv_read_start(
                reinterpret_cast<uv_stream_t*>(&tcp_handle_),
                [](uv_handle_t* h, size_t suggested_size, uv_buf_t* buf) {
                    auto* ctx = static_cast<LibuvSocket*>(h->data);
                    *buf = ctx->read_buffer_allocator_.Alloc(suggested_size);
                },
                [](uv_stream_t* s, ssize_t n, const uv_buf_t* b) {
                    auto* ctx = static_cast<LibuvSocket*>(s->data);
                    ctx->OnRead(n, b);
                    ctx->read_buffer_allocator_.Free(*b);
                });
    }

    void Accept(uv_stream_t* server_handle) {
        DCHECK(event_loop_->IsOnLoopThread());
        auto result = uv_accept(server_handle, reinterpret_cast<uv_stream_t*>(&tcp_handle_));
        VLOG(1) << "accept: " << UvErrToAbslStatus(result);
        if (result == 0) {
            is_connected_ = true;

            struct sockaddr_storage addr;
            int namelen = sizeof(addr);
            const int peer_result = uv_tcp_getpeername(
                    &tcp_handle_, reinterpret_cast<struct sockaddr*>(&addr), &namelen);
            if (peer_result != 0) {
                LOG(WARNING) << "Failed to get peer name: " << uv_strerror(peer_result);
            } else {
                auto ep = ToEndpoint(*reinterpret_cast<struct sockaddr*>(&addr));
                if (ep.ok()) {
                    endpoint_ = *std::move(ep);
                } else {
                    LOG(WARNING) << "Failed to get endpoint: " << ep.status();
                }
            }
        } else {
            LOG(WARNING) << "Failed to accept incoming connection: " << uv_strerror(result);
            uv_close(reinterpret_cast<uv_handle_t*>(&tcp_handle_), nullptr);
        }
    }

    void OnRead(ssize_t nread, const uv_buf_t* buf) {
        DCHECK(on_read_) << "`mOnRead` must be set to prevent loss of data.";

        VLOG(2) << "on_read: " << nread << " : " << UvErrToAbslStatus(static_cast<int>(nread));
        if (nread >= 0) {
            // Success path (nread > 0) or no-op (nread == 0).
            on_read_({buf->base, static_cast<size_t>(nread)}, absl::OkStatus());
        } else if (nread == UV_EOF) {
            Close();
        } else {
            // Error path (nread < 0). This is a fatal, unrecoverable stream error.
            on_read_({}, UvErrToAbslStatus(static_cast<int>(nread)));
            Close();
        }
    }

    void OnConnect(int status) {
        VLOG(1) << "on_connect: " << UvErrToAbslStatus(status);
        if (on_connected_) {
            if (status == 0) {
                is_connected_ = true;
                on_connected_(*this, absl::OkStatus());
                DCHECK(on_read_)
                        << "`mOnRead` must be set by `mOnConnected` to prevent loss of data.";
                StartReading();
            } else {
                // Failure case
                on_connected_(*this, UvErrToAbslStatus(status));
            }
        }
    }

    EventLoop* const event_loop_;
    uv_loop_t* const loop_;
    Endpoint endpoint_;
    ReadBufferAllocator<4> read_buffer_allocator_;
    uv_tcp_t tcp_handle_;

    OnReadCallback on_read_;
    OnCloseCallback on_close_;
    OnConnectCallback on_connected_;

    const bool is_incoming_;
    bool is_connected_ = false;
};

class LibuvServer : public AsyncSocketServer, public std::enable_shared_from_this<LibuvServer> {
    struct Private {};

  public:
    // Factory to create a LibuvServer. Returns nullptr on failure.
    static std::shared_ptr<LibuvServer> Create(EventLoop* loop, const Endpoint& endpoint,
                                               ConnectCallback cb) {
        DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";
        auto server = std::make_shared<LibuvServer>(loop, std::move(cb), Private());

        if (server->BindAndListen(endpoint)) {
            return server;
        }

        server->Close();
        return nullptr;
    }

    LibuvServer(EventLoop* loop, ConnectCallback cb, Private)
            : event_loop_(loop)
            , loop_(static_cast<uv_loop_t*>(loop->GetRawLoop()))
            , connect_callback_(std::move(cb)) {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be constructed on loop thread";
        uv_tcp_init(loop_, &server_handle_);
        server_handle_.data = this;
    }

    ~LibuvServer() override {
        DCHECK(uv_is_closing((const uv_handle_t*)&server_handle_))
                << "LibuvServer destroyed without calling close() first!";
    }

    Endpoint GetEndpoint() const override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        struct sockaddr_storage addr = {};
        int len = sizeof(addr);
        if (const int getsockname_result = ::uv_tcp_getsockname(
                    &server_handle_, reinterpret_cast<struct sockaddr*>(&addr), &len)) {
            LOG(WARNING) << "getsockname error: " << UvErrToAbslStatus(getsockname_result);
            return {};
        }

        auto ep = network::ToEndpoint(*reinterpret_cast<struct sockaddr*>(&addr));
        if (!ep.ok()) {
            LOG(ERROR) << "Could not get the server endpoint";
            return {};
        }

        return *std::move(ep);
    }

    void Close() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        if (!uv_is_closing(reinterpret_cast<const uv_handle_t*>(&server_handle_))) {
            is_listening_ = false;
            server_handle_.data = new std::shared_ptr<LibuvServer>(shared_from_this());

            uv_close(reinterpret_cast<uv_handle_t*>(&server_handle_), [](uv_handle_t* handle) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvServer>*>(handle->data);
                if ((*self_ptr)->on_close_) {
                    (*self_ptr)->on_close_();
                }
                delete self_ptr;
            });
        }
    }

    void SetOnCloseCallback(AsyncSocket::OnCloseCallback cb) {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_close_ = std::move(cb);
    }

    EventLoop* GetLoop() const override { return event_loop_; }

  private:
    bool BindAndListen(const Endpoint& endpoint) {
        const struct sockaddr_storage addr = ToSockaddr(endpoint);
        if (addr.ss_family == AF_UNSPEC) {
            LOG(ERROR) << "Failed to convert endpoint to sockaddr: " << ToString(endpoint);
            return false;
        }

        if (uv_tcp_bind(&server_handle_, reinterpret_cast<const struct sockaddr*>(&addr), 0) != 0) {
            LOG(ERROR) << "Failed to bind to " << ToString(endpoint);
            return false;
        }

        const int listen_res = uv_listen(reinterpret_cast<uv_stream_t*>(&server_handle_), 128,
                                         [](uv_stream_t* s, int status) {
                                             if (status < 0) {
                                                 LOG(WARNING) << "Listen error: "
                                                              << UvErrToAbslStatus(status);
                                                 return;
                                             }
                                             static_cast<LibuvServer*>(s->data)->OnNewConnection(s);
                                         });

        if (listen_res != 0) {
            LOG(ERROR) << "Failed to listen on " << ToString(endpoint) << ": "
                       << UvErrToAbslStatus(listen_res);
            return false;
        }

        is_listening_ = true;
        return true;
    }

    void OnNewConnection(uv_stream_t* server) {
        if (!is_listening_) return;

        auto client = std::make_shared<LibuvSocket>(event_loop_);
        client->Accept(server);
        uv_tcp_nodelay(&client->tcp_handle_, 1);  // Enable TCP_NODELAY for accepted socket

        // At this point, client.use_count() is 1.
        const bool accepted = connect_callback_(client);

        // Now, check what the user did.
        if (accepted) {
            // If the callback returned true but didn't take ownership,
            // tsk, tsk.
            if (client.use_count() == 1) {
                LOG(WARNING) << "onConnectCallback returned true but did not retain "
                             << "ownership of the socket. The connection will be closed "
                             << "to prevent it from being abandoned.";
                client->Close();
            } else {
                DCHECK(client->on_read_)
                        << "`mOnRead` must be set by `mConnectCallback` to prevent loss of data.";
                client->StartReading();
            }
        } else {
            client->Close();
        }
    }

    EventLoop* event_loop_;
    uv_loop_t* loop_;
    AsyncSocket::OnCloseCallback on_close_;
    ConnectCallback connect_callback_;
    uv_tcp_t server_handle_;
    bool is_listening_ = false;
};

}  // namespace

// =================================================================
//          LibuvSocketFactory Implementation
// =================================================================

std::shared_ptr<AsyncSocketServer> LibuvAsyncSocketFactory::CreateServer(
        EventLoop* loop, const Endpoint& endpoint,
        AsyncSocketServer::ConnectCallback connect_callback) {
    DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";
    return LibuvServer::Create(loop, endpoint, std::move(connect_callback));
}

std::shared_ptr<AsyncSocket> LibuvAsyncSocketFactory::CreateSocket(EventLoop* loop,
                                                                   const Endpoint& endpoint) {
    DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";
    return std::make_shared<LibuvSocket>(loop, endpoint);
}

}  // namespace goldfish::async
