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
#include "goldfish/cpp/overloaded.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {

using goldfish::cpp::Overloaded;
using goldfish::network::Endpoint;
using goldfish::network::Ipv4Endpoint;
using goldfish::network::Ipv6Endpoint;
using goldfish::network::ToEndpoint;
using goldfish::network::UnEndpoint;

namespace {

int UvIsClosing(const uv_stream_t* stream) {
    return ::uv_is_closing(reinterpret_cast<const uv_handle_t*>(stream));
}

void UvIsClosingChecked(const uv_stream_t* stream) {
    DCHECK(UvIsClosing(stream)) << "LibuvSocket destroyed without calling close() first!";
}

void CrashIfUvFailed(const int uv_result, const char* const what) {
    CHECK(!uv_result) << what << " failed with " << uv_strerror(uv_result);
}

struct WriteReqT {
    uv_write_t req;
    uv_buf_t buf;
    AsyncSocket::OnSendCallback on_send;

    static WriteReqT* Create(const char* buffer_data, size_t buffer_size,
                             AsyncSocket::OnSendCallback on_send) {
        const size_t total_size = sizeof(WriteReqT) + buffer_size;
        void* raw_memory = malloc(total_size);  // NOLINT
        DCHECK(raw_memory) << "Ran out of memory while creating packet";
        auto* write_req = new (raw_memory) WriteReqT();
        char* write_buffer = reinterpret_cast<char*>(write_req) + sizeof(WriteReqT);
        memcpy(write_buffer, buffer_data, buffer_size);

        write_req->buf = uv_buf_init(write_buffer, buffer_size);
        write_req->on_send = std::move(on_send);

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
    // --- Configuration Methods ---
    void SetOnReadCallbackNoFlowControl(OnReadCallback on_read) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_read_ = std::move(on_read);
    }

    void OnFlowControlEvent(const bool enable_reading) override {
        auto weak_self = std::weak_ptr<LibuvSocket>(shared_from_this());
        event_loop_
                ->Post([enable_reading, weak_self = std::move(weak_self)]() {
                    if (const auto self = weak_self.lock()) {
                        if (enable_reading) {
                            self->StartReading();
                        } else {
                            uv_read_stop(self->GetUvSocketStream());
                        }
                    }
                })
                .IgnoreError();
    }

    void SetOnCloseCallback(OnCloseCallback on_close) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_close_ = std::move(on_close);
    }

    void SetOnConnectedCallback(OnConnectCallback on_connected) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_connected_ = std::move(on_connected);
    }

    // --- I/O Methods ---
    absl::Status Send(const char* buffer, size_t buffer_size, OnSendCallback on_send) override {
        DCHECK(event_loop_->IsOnLoopThread()) << "buffer_sizelled on loop thread";

        uv_stream_t* stream = GetUvSocketStream();
        if (!is_connected_ || UvIsClosing(stream)) {
            return UvErrToAbslStatus(UV_ENOTCONN);
        }

        auto* write_req = WriteReqT::Create(buffer, buffer_size, std::move(on_send));
        uv_write(&write_req->req, stream, &write_req->buf, 1, [](uv_write_t* req, int s) {
            auto* w = reinterpret_cast<WriteReqT*>(req);
            w->on_send(UvErrToAbslStatus(s));
            WriteReqT::Destroy(w);
        });

        return absl::OkStatus();
    }

    void Close() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        is_connected_ = false;
        auto* handle = GetUvSocketStreamAsHandle();
        if (!uv_is_closing(handle)) {
            handle->data = new std::shared_ptr<LibuvSocket>(shared_from_this());
            uv_close(handle, [](uv_handle_t* h) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(h->data);
                if ((*self_ptr)->on_close_) {
                    (*self_ptr)->on_close_();
                }
                delete self_ptr;
            });
        }
    }

    bool Connected() const override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        return is_connected_ &&
               !uv_is_closing(const_cast<LibuvSocket*>(this)->GetUvSocketStreamAsHandle());
    }

    EventLoop* GetLoop() const override { return event_loop_; }

    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[uvs %s%s %s L:%p]", (is_incoming_ ? "<-" : "->"),
                     (is_connected_ ? "+" : "-"), ToString(endpoint_), GetLoop());
    }

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

    void StartReading() {
        DCHECK(event_loop_->IsOnLoopThread());

        uv_stream_t* stream = GetUvSocketStream();
        if (UvIsClosing(stream)) return;

        uv_read_start(
                stream,
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

    void Accept(uv_stream_t* server_stream, uv_stream_t* socket_stream) {
        DCHECK(event_loop_->IsOnLoopThread());

        auto result = uv_accept(server_stream, socket_stream);
        VLOG(1) << "accept: " << UvErrToAbslStatus(result);
        if (result == 0) {
            is_connected_ = true;

            auto ep = GetMyEndpoint();
            if (ep.ok()) {
                if (ep.ok()) {
                    endpoint_ = *std::move(ep);
                } else {
                    LOG(WARNING) << "Failed to get endpoint: " << ep.status();
                }
            }
        } else {
            LOG(WARNING) << "Failed to accept incoming connection: " << uv_strerror(result);
            uv_close(reinterpret_cast<uv_handle_t*>(socket_stream), nullptr);
        }
    }

    void OnRead(ssize_t nread, const uv_buf_t* buf) {
        DCHECK(on_read_) << "`mOnRead` must be set to prevent loss of data.";

        VLOG(2) << "on_read: " << nread << " : " << uv_strerror(static_cast<int>(nread));
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
        VLOG(1) << "on_connect: " << uv_strerror(status);
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

  protected:
    uv_handle_t* GetUvSocketStreamAsHandle() {
        return reinterpret_cast<uv_handle_t*>(GetUvSocketStream());
    }

    virtual uv_stream_t* GetUvSocketStream() = 0;
    virtual absl::StatusOr<Endpoint> GetMyEndpoint() const = 0;

    LibuvSocket(EventLoop* loop, Endpoint endpoint, const bool is_incoming)
            : event_loop_(loop)
            , loop_(static_cast<uv_loop_t*>(loop->GetRawLoop()))
            , endpoint_(std::move(endpoint))
            , read_buffer_allocator_(16384)
            , is_incoming_(is_incoming) {}

    EventLoop* const event_loop_;
    uv_loop_t* const loop_;
    Endpoint endpoint_;
    ReadBufferAllocator<4> read_buffer_allocator_;

    OnReadCallback on_read_;
    OnCloseCallback on_close_;
    OnConnectCallback on_connected_;

    const bool is_incoming_;
    bool is_connected_ = false;
};

class TcpLibuvSocket : public LibuvSocket {
  public:
    explicit TcpLibuvSocket(EventLoop* loop) : TcpLibuvSocket(loop, {}, /*is_incoming=*/true) {}

    TcpLibuvSocket(EventLoop* loop, Endpoint endpoint)
            : TcpLibuvSocket(loop, std::move(endpoint), /*is_incoming=*/false) {}

    ~TcpLibuvSocket() override {
        UvIsClosingChecked(reinterpret_cast<uv_stream_t*>(&socket_stream_));
    }

    uv_stream_t* GetUvSocketStream() override {
        return reinterpret_cast<uv_stream_t*>(&socket_stream_);
    }

  protected:
    absl::Status Connect() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        if (is_connected_) {
            return absl::OkStatus();
        }

        auto* self_ptr = new std::shared_ptr<LibuvSocket>(shared_from_this());
        auto* connect_req = new uv_connect_t();
        connect_req->data = self_ptr;

        struct sockaddr_storage addr = ToSockaddr(endpoint_);
        const int connect_res = uv_tcp_connect(
                connect_req, &socket_stream_, reinterpret_cast<const struct sockaddr*>(&addr),
                [](uv_connect_t* req, int s) {
                    auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(req->data);
                    (*self_ptr)->OnConnect(s);
                    delete self_ptr;
                    delete req;
                });
        if (connect_res) {
            LOG(ERROR) << "uv_tcp_connect failed: " << uv_strerror(connect_res);
            delete self_ptr;
            delete connect_req;
            return UvErrToAbslStatus(connect_res);
        }

        return absl::OkStatus();
    }

    absl::StatusOr<Endpoint> GetMyEndpoint() const override {
        struct sockaddr_storage addr;
        int namelen = sizeof(addr);
        const int peer_result = uv_tcp_getpeername(
                &socket_stream_, reinterpret_cast<struct sockaddr*>(&addr), &namelen);
        if (peer_result != 0) {
            LOG(WARNING) << "Failed to get peer name: " << uv_strerror(peer_result);
            return UvErrToAbslStatus(peer_result);
        }

        auto ep = ToEndpoint(*reinterpret_cast<struct sockaddr*>(&addr));
        if (!ep.ok()) {
            return ep.status();
        }

        return *std::move(ep);
    }

  private:
    TcpLibuvSocket(EventLoop* loop, Endpoint endpoint, const bool is_incoming)
            : LibuvSocket(loop, std::move(endpoint), is_incoming) {
        CrashIfUvFailed(uv_tcp_init(loop_, &socket_stream_), "uv_tcp_init");
        CrashIfUvFailed(uv_tcp_nodelay(&socket_stream_, 1), "uv_tcp_nodelay");
        socket_stream_.data = this;
    }

    uv_tcp_t socket_stream_;
};

class UnLibuvSocket : public LibuvSocket {
  public:
    explicit UnLibuvSocket(EventLoop* loop) : UnLibuvSocket(loop, {}, /*is_incoming=*/true) {}

    UnLibuvSocket(EventLoop* loop, Endpoint endpoint)
            : UnLibuvSocket(loop, std::move(endpoint), /*is_incoming=*/false) {}

    ~UnLibuvSocket() override {
        UvIsClosingChecked(reinterpret_cast<uv_stream_t*>(&socket_stream_));
    }

    uv_stream_t* GetUvSocketStream() override {
        return reinterpret_cast<uv_stream_t*>(&socket_stream_);
    }

  protected:
    absl::Status Connect() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        DCHECK(std::holds_alternative<UnEndpoint>(endpoint_));

        if (is_connected_) {
            return absl::OkStatus();
        }

        auto* self_ptr = new std::shared_ptr<LibuvSocket>(shared_from_this());
        auto* connect_req = new uv_connect_t();
        connect_req->data = self_ptr;

        const auto& ep = std::get<UnEndpoint>(endpoint_);
        const int connect_res = uv_pipe_connect2(
                connect_req, &socket_stream_, ep.Address().data(), ep.Address().size(),
                UV_PIPE_NO_TRUNCATE, [](uv_connect_t* req, int s) {
                    auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(req->data);
                    (*self_ptr)->OnConnect(s);
                    delete self_ptr;
                    delete req;
                });
        if (connect_res) {
            LOG(ERROR) << "uv_tcp_connect failed: " << uv_strerror(connect_res);
            delete self_ptr;
            delete connect_req;
            return UvErrToAbslStatus(connect_res);
        }

        return absl::OkStatus();
    }

    absl::StatusOr<Endpoint> GetMyEndpoint() const override {
        size_t namelen = 0;
        std::string name(1, '?');
        int getpeername_res = uv_pipe_getpeername(&socket_stream_, name.data(), &namelen);
        if (getpeername_res && (getpeername_res != UV_ENOBUFS)) {
            return UvErrToAbslStatus(getpeername_res);
        }

        if (!namelen) {
            return Endpoint(UnEndpoint::MakeEmpty());
        }

        name.resize(namelen);
        getpeername_res = uv_pipe_getpeername(&socket_stream_, name.data(), &namelen);
        if (getpeername_res) {
            return UvErrToAbslStatus(getpeername_res);
        }

        name.resize(namelen);
        return Endpoint(*UnEndpoint::Create(std::move(name)));
    }

  private:
    UnLibuvSocket(EventLoop* loop, Endpoint endpoint, const bool is_incoming)
            : LibuvSocket(loop, std::move(endpoint), is_incoming) {
        CrashIfUvFailed(uv_pipe_init(loop_, &socket_stream_, 0), "uv_pipe_init");
        socket_stream_.data = this;
    }

    uv_pipe_t socket_stream_;
};

class LibuvServer : public AsyncSocketServer, public std::enable_shared_from_this<LibuvServer> {
  public:
    LibuvServer(EventLoop* loop, ConnectCallback connect_callback)
            : event_loop_(loop)
            , loop_(static_cast<uv_loop_t*>(loop->GetRawLoop()))
            , connect_callback_(std::move(connect_callback)) {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be constructed on loop thread";
    }

    void Close() override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        auto* server_handle = reinterpret_cast<uv_handle_t*>(GetServerStream());
        if (!uv_is_closing(server_handle)) {
            is_listening_ = false;
            server_handle->data = new std::shared_ptr<LibuvServer>(shared_from_this());

            uv_close(server_handle, [](uv_handle_t* handle) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvServer>*>(handle->data);
                if ((*self_ptr)->on_close_) {
                    (*self_ptr)->on_close_();
                }
                delete self_ptr;
            });
        }
    }

    void SetOnCloseCallback(AsyncSocket::OnCloseCallback close_callback) {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";
        on_close_ = std::move(close_callback);
    }

    EventLoop* GetLoop() const override { return event_loop_; }

    void OnNewConnection(uv_stream_t* server) {
        if (!is_listening_) return;

        auto client = OnNewConnectionImpl(server, event_loop_);
        // At this point, client.use_count() is 1.

        if (connect_callback_(client)) {
            // Now, check what the user did in `connect_callback_`.
            // If the callback returned true but didn't take ownership,
            // tsk, tsk.
            if (client.use_count() == 1) {
                LOG(WARNING) << "onConnectCallback returned true but did not retain "
                             << "ownership of the socket. The connection will be closed "
                             << "to prevent it from being abandoned.";
                client->Close();
            } else {
                client->StartReading();
            }
        } else {
            client->Close();
        }
    }

    bool StartListening(uv_stream_t* server, const Endpoint& endpoint) {
        const int listen_res = uv_listen(server, 128, [](uv_stream_t* s, int status) {
            if (status < 0) {
                LOG(WARNING) << "Listen error: " << uv_strerror(status);
                return;
            }
            static_cast<LibuvServer*>(s->data)->OnNewConnection(s);
        });
        if (listen_res) {
            LOG(ERROR) << "Failed to listen on " << ToString(endpoint) << ": "
                       << uv_strerror(listen_res);
            return false;
        }

        is_listening_ = true;
        return true;
    }

    virtual uv_stream_t* GetServerStream() = 0;
    virtual std::shared_ptr<LibuvSocket> OnNewConnectionImpl(uv_stream_t* server,
                                                             EventLoop* loop) = 0;

  protected:
    EventLoop* const event_loop_;
    uv_loop_t* const loop_;
    const ConnectCallback connect_callback_;
    AsyncSocket::OnCloseCallback on_close_;
    bool is_listening_ = false;
};

class TcpLibuvServer : public LibuvServer {
  public:
    TcpLibuvServer(EventLoop* loop, ConnectCallback connect_callback)
            : LibuvServer(loop, std::move(connect_callback)) {
        CrashIfUvFailed(uv_tcp_init(loop_, &server_stream_), "uv_tcp_init");
        server_stream_.data = this;
    }

    ~TcpLibuvServer() override {
        UvIsClosingChecked(reinterpret_cast<uv_stream_t*>(&server_stream_));
    }

    // Factory to create a LibuvServer. Returns nullptr on failure.
    static std::shared_ptr<LibuvServer> Create(EventLoop* loop, const Endpoint& endpoint,
                                               ConnectCallback connect_callback) {
        DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";

        auto server = std::make_shared<TcpLibuvServer>(loop, std::move(connect_callback));
        if (server->BindAndListen(endpoint)) {
            return server;
        }

        server->Close();
        return nullptr;
    }

    uv_stream_t* GetServerStream() override {
        return reinterpret_cast<uv_stream_t*>(&server_stream_);
    }

    std::shared_ptr<LibuvSocket> OnNewConnectionImpl(uv_stream_t* server,
                                                     EventLoop* loop) override {
        auto client = std::make_shared<TcpLibuvSocket>(loop);
        client->Accept(server, client->GetUvSocketStream());
        return client;
    }

    Endpoint GetEndpoint() const override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        struct sockaddr_storage addr = {};
        int len = sizeof(addr);
        if (const int getsockname_result = uv_tcp_getsockname(
                    &server_stream_, reinterpret_cast<struct sockaddr*>(&addr), &len)) {
            LOG(WARNING) << "getsockname error: " << uv_strerror(getsockname_result);
            return {};
        }

        auto ep = network::ToEndpoint(*reinterpret_cast<struct sockaddr*>(&addr));
        if (!ep.ok()) {
            LOG(ERROR) << "Could not get the server endpoint";
            return {};
        }

        return *std::move(ep);
    }

    bool BindAndListen(const Endpoint& endpoint) {
        const struct sockaddr_storage addr = ToSockaddr(endpoint);
        const int res =
                uv_tcp_bind(&server_stream_, reinterpret_cast<const struct sockaddr*>(&addr), 0);
        if (res) {
            LOG(ERROR) << "Failed to bind to " << ToString(endpoint) << ": " << uv_strerror(res);
            return false;
        }

        return StartListening(reinterpret_cast<uv_stream_t*>(&server_stream_), endpoint);
    }

  private:
    uv_tcp_t server_stream_;
};

class UnLibuvServer : public LibuvServer {
  public:
    UnLibuvServer(EventLoop* loop, ConnectCallback connect_callback)
            : LibuvServer(loop, std::move(connect_callback)) {
        CrashIfUvFailed(uv_pipe_init(loop_, &server_stream_, 0), "uv_pipe_init");
        server_stream_.data = this;
    }

    ~UnLibuvServer() override {
        UvIsClosingChecked(reinterpret_cast<uv_stream_t*>(&server_stream_));
    }

    // Factory to create a LibuvServer. Returns nullptr on failure.
    static std::shared_ptr<LibuvServer> Create(EventLoop* loop, const UnEndpoint& endpoint,
                                               ConnectCallback connect_callback) {
        DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";

        auto server = std::make_shared<UnLibuvServer>(loop, std::move(connect_callback));
        if (server->BindAndListen(endpoint)) {
            return server;
        }

        server->Close();
        return nullptr;
    }

    uv_stream_t* GetServerStream() override {
        return reinterpret_cast<uv_stream_t*>(&server_stream_);
    }

    std::shared_ptr<LibuvSocket> OnNewConnectionImpl(uv_stream_t* server,
                                                     EventLoop* loop) override {
        auto client = std::make_shared<UnLibuvSocket>(loop);
        client->Accept(server, client->GetUvSocketStream());
        return client;
    }

    Endpoint GetEndpoint() const override {
        DCHECK(event_loop_->IsOnLoopThread()) << "Must be called on loop thread";

        size_t namelen = 0;
        std::string name(1, '?');
        int getsockname_result = uv_pipe_getsockname(&server_stream_, name.data(), &namelen);
        if (getsockname_result && (getsockname_result != UV_ENOBUFS)) {
            LOG(WARNING) << "getsockname error: " << uv_strerror(getsockname_result);
            return UnEndpoint::MakeEmpty();
        }

        if (!namelen) {
            return UnEndpoint::MakeEmpty();
        }

        name.resize(namelen);
        getsockname_result = uv_pipe_getsockname(&server_stream_, name.data(), &namelen);
        if (getsockname_result) {
            LOG(WARNING) << "getsockname error: " << uv_strerror(getsockname_result);
            return UnEndpoint::MakeEmpty();
        }

        name.resize(namelen);
        return *UnEndpoint::Create(std::move(name));
    }

  private:
    bool BindAndListen(const UnEndpoint& endpoint) {
        DCHECK(server_stream_.data == this);

        const int res = uv_pipe_bind2(&server_stream_, endpoint.Address().data(),
                                      endpoint.Address().size(), UV_PIPE_NO_TRUNCATE);
        if (res) {
            LOG(ERROR) << "Failed to bind to " << ToString(endpoint) << ": " << uv_strerror(res);
            return false;
        }

        return StartListening(reinterpret_cast<uv_stream_t*>(&server_stream_), endpoint);
    }

    uv_pipe_t server_stream_;
};

}  // namespace

// =================================================================
//          LibuvSocketFactory Implementation
// =================================================================

std::shared_ptr<AsyncSocketServer> LibuvAsyncSocketFactory::CreateServer(
        EventLoop* loop, const Endpoint& endpoint,
        AsyncSocketServer::ConnectCallback connect_callback) {
    DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";

    return std::visit(
            Overloaded{
                [loop,
                 &connect_callback](const Ipv4Endpoint& ep) -> std::shared_ptr<AsyncSocketServer> {
                    return TcpLibuvServer::Create(loop, Endpoint(ep), std::move(connect_callback));
                },
                [loop,
                 &connect_callback](const Ipv6Endpoint& ep) -> std::shared_ptr<AsyncSocketServer> {
                    return TcpLibuvServer::Create(loop, Endpoint(ep), std::move(connect_callback));
                },
                [loop,
                 &connect_callback](const UnEndpoint& ep) -> std::shared_ptr<AsyncSocketServer> {
                    return UnLibuvServer::Create(loop, ep, std::move(connect_callback));
                },
            },
            endpoint);
}

std::shared_ptr<AsyncSocket> LibuvAsyncSocketFactory::CreateSocket(EventLoop* loop,
                                                                   const Endpoint& endpoint) {
    DCHECK(loop->IsOnLoopThread()) << "Factory must be used on loop thread";

    return std::visit(Overloaded{
                          [loop](const Ipv4Endpoint& ep) -> std::shared_ptr<AsyncSocket> {
                              return std::make_shared<TcpLibuvSocket>(loop, Endpoint(ep));
                          },
                          [loop](const Ipv6Endpoint& ep) -> std::shared_ptr<AsyncSocket> {
                              return std::make_shared<TcpLibuvSocket>(loop, Endpoint(ep));
                          },
                          [loop](const UnEndpoint& ep) -> std::shared_ptr<AsyncSocket> {
                              return std::make_shared<UnLibuvSocket>(loop, Endpoint(ep));
                          },
                      },
                      endpoint);
}

}  // namespace goldfish::async
