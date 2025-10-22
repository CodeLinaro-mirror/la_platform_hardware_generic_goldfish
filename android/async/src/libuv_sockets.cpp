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
#include <sys/types.h>
#include <uv.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/dns_resolver.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

struct write_req_t {
    uv_write_t req;
    uv_buf_t buf;
    AsyncSocket::OnSendCallback cb;

    static write_req_t* create(const char* bufferData, size_t bufferSize,
                               AsyncSocket::OnSendCallback cb) {
        size_t totalSize = sizeof(write_req_t) + bufferSize;
        void* raw_memory = malloc(totalSize);
        DCHECK(raw_memory) << "Ran out of memory while creating packet";
        write_req_t* writeReq = new (raw_memory) write_req_t();
        char* write_buffer = reinterpret_cast<char*>(writeReq) + sizeof(write_req_t);
        memcpy(write_buffer, bufferData, bufferSize);

        writeReq->buf = uv_buf_init(write_buffer, bufferSize);
        writeReq->cb = std::move(cb);

        return writeReq;
    }

    static void destroy(write_req_t* w) {
        w->~write_req_t();
        free(w);
    }
};

// =================================================================
//                 CONCRETE IMPLEMENTATION CLASSES
// =================================================================

class LibuvSocket : public AsyncSocket, public std::enable_shared_from_this<LibuvSocket> {
  public:
    explicit LibuvSocket(EventLoop* loop) : LibuvSocket(loop, /*isIncoming=*/true) {}

    LibuvSocket(EventLoop* loop, const struct sockaddr* addr)
            : LibuvSocket(loop, /*isIncoming=*/false) {
        if (addr->sa_family == AF_INET) {
            // Copy IPv4 address
            memcpy(&mAddr, addr, sizeof(sockaddr_in));
        } else if (addr->sa_family == AF_INET6) {
            // Copy IPv6 address
            memcpy(&mAddr, addr, sizeof(sockaddr_in6));
        } else {
            memset(&mAddr, 0, sizeof(mAddr));
        }
    }

    ~LibuvSocket() override {
        assert(uv_is_closing((const uv_handle_t*)&mTcpHandle) &&
               "LibuvSocket destroyed without calling close() first!");
    }

    // --- Configuration Methods ---
    void setOnReadCallback(OnReadCallback cb) override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnRead = std::move(cb);
    }

    void setOnCloseCallback(OnCloseCallback cb) override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnClose = std::move(cb);
    }

    void setOnConnectedCallback(OnConnectCallback cb) override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnConnected = std::move(cb);
    }

    // --- I/O Methods ---
    absl::Status send(const char* buffer, size_t bufferSize, OnSendCallback cb) override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");

        if (!mIsConnected || uv_is_closing((const uv_handle_t*)&mTcpHandle)) {
            return UvErrToAbslStatus(UV_ENOTCONN);
        }

        auto* writeReq = write_req_t::create(buffer, bufferSize, std::move(cb));

        uv_write(&writeReq->req, (uv_stream_t*)&mTcpHandle, &writeReq->buf, 1,
                 [](uv_write_t* req, int s) {
                     auto* w = reinterpret_cast<write_req_t*>(req);
                     w->cb(UvErrToAbslStatus(s));
                     write_req_t::destroy(w);
                 });
        return absl::OkStatus();
    }

    void close() override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mIsConnected = false;

        if (!uv_is_closing((const uv_handle_t*)&mTcpHandle)) {
            mTcpHandle.data = new std::shared_ptr<LibuvSocket>(shared_from_this());
            uv_close((uv_handle_t*)&mTcpHandle, [](uv_handle_t* h) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(h->data);
                if ((*self_ptr)->mOnClose) {
                    (*self_ptr)->mOnClose();
                }
                delete self_ptr;
            });
        }
    }

    absl::Status connect() override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        if (mIsConnected) {
            return absl::OkStatus();
        }

        auto* connectReq = new uv_connect_t();
        connectReq->data = new std::shared_ptr<LibuvSocket>(shared_from_this());

        uv_tcp_connect(connectReq, &mTcpHandle, (const struct sockaddr*)&mAddr,
                       [](uv_connect_t* req, int s) {
                           auto* self_ptr = static_cast<std::shared_ptr<LibuvSocket>*>(req->data);
                           (*self_ptr)->on_connect(s);
                           delete self_ptr;
                           delete req;
                       });
        return absl::OkStatus();
    }

    bool connected() const override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        return mIsConnected && !uv_is_closing((const uv_handle_t*)&mTcpHandle);
    }

    EventLoop* getLoop() const override { return mEventLoop; }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        char ip[INET6_ADDRSTRLEN];
        int port = 0;

        if (mAddr.ss_family == AF_INET) {
            const auto* addr_in = reinterpret_cast<const sockaddr_in*>(&mAddr);
            uv_ip4_name(addr_in, ip, sizeof(ip));
            port = ntohs(addr_in->sin_port);
        } else if (mAddr.ss_family == AF_INET6) {
            const auto* addr_in6 = reinterpret_cast<const sockaddr_in6*>(&mAddr);
            uv_ip6_name(addr_in6, ip, sizeof(ip));
            port = ntohs(addr_in6->sin6_port);
        } else {
            absl::Format(&s, "[uvs ? L:%p]", getLoop());
            return;
        }

        absl::Format(&s, "[uvs %s%s %s:%d L:%p]", mIsIncoming ? "<-" : "->",
                     mIsConnected ? "+" : "-", ip, port, getLoop());
    }

  private:
    friend class LibuvServer;

    LibuvSocket(EventLoop* loop, const bool isIncoming)
            : mEventLoop(loop)
            , mLoop(static_cast<uv_loop_t*>(loop->getRawLoop()))
            , mIsIncoming(isIncoming) {
        uv_tcp_init(mLoop, &mTcpHandle);
        mTcpHandle.data = this;
    }

    void startReading() {
        assert(mEventLoop->isOnLoopThread());
        if (uv_is_closing((const uv_handle_t*)&mTcpHandle)) return;

        uv_read_start((uv_stream_t*)&mTcpHandle,
                      [](uv_handle_t*, size_t size, uv_buf_t* buf) {
                          *buf = uv_buf_init(new char[size], size);
                      },
                      [](uv_stream_t* s, ssize_t n, const uv_buf_t* b) {
                          LibuvSocket* ctx = static_cast<LibuvSocket*>(s->data);
                          ctx->on_read(n, b);
                          delete[] b->base;
                      });
    }

    void accept(uv_stream_t* server_handle) {
        assert(mEventLoop->isOnLoopThread());
        auto result = uv_accept(server_handle, (uv_stream_t*)&mTcpHandle);
        VLOG(1) << "accept: " << UvErrToAbslStatus(result);
        if (result == 0) {
            mIsConnected = true;
            int namelen = sizeof(mAddr);
            int peer_result = uv_tcp_getpeername(&mTcpHandle, (struct sockaddr*)&mAddr, &namelen);
            if (peer_result != 0) {
                LOG(WARNING) << "Failed to get peer name: " << uv_strerror(peer_result);
                memset(&mAddr, 0, sizeof(mAddr));
            }
        } else {
            LOG(WARNING) << "Failed to accept incoming connection: " << uv_strerror(result);
            uv_close((uv_handle_t*)&mTcpHandle, nullptr);
        }
    }

    void on_read(ssize_t nread, const uv_buf_t* buf) {
        assert(mOnRead && "`mOnRead` must be set set to prevent loss of data.");
        VLOG(2) << "on_read: " << nread << " : " << UvErrToAbslStatus(nread);
        if (nread >= 0) {
            // Success path (nread > 0) or no-op (nread == 0).
            mOnRead({buf->base, (size_t)nread}, absl::OkStatus());
        } else {
            // Error path (nread < 0). This is a fatal, unrecoverable stream error.
            mOnRead({}, UvErrToAbslStatus(nread));
            close();
        }
    }

    void on_connect(int status) {
        VLOG(1) << "on_connect: " << UvErrToAbslStatus(status);
        if (mOnConnected) {
            if (status == 0) {
                mIsConnected = true;
                mOnConnected(*this, absl::OkStatus());
                CHECK(mOnRead)
                        << "`mOnRead` must be set by `mOnConnected` to prevent loss of data.";
                startReading();
            } else {
                // Failure case
                mOnConnected(*this, UvErrToAbslStatus(status));
            }
        }
    }

    EventLoop* const mEventLoop;
    uv_loop_t* const mLoop;
    uv_tcp_t mTcpHandle;
    sockaddr_storage mAddr;

    OnReadCallback mOnRead;
    OnCloseCallback mOnClose;
    OnConnectCallback mOnConnected;

    const bool mIsIncoming;
    bool mIsConnected = false;
};

class LibuvServer : public AsyncSocketServer, public std::enable_shared_from_this<LibuvServer> {
    struct Private {};

  public:
    // Factory to create a LibuvServer. Returns nullptr on failure.
    static std::shared_ptr<LibuvServer> create(EventLoop* loop, const std::string& address,
                                               ConnectCallback cb);

    LibuvServer(EventLoop* loop, ConnectCallback cb, Private)
            : mEventLoop(loop)
            , mLoop(static_cast<uv_loop_t*>(loop->getRawLoop()))
            , mConnectCallback(std::move(cb)) {
        assert(mEventLoop->isOnLoopThread() && "Must be constructed on loop thread");
        uv_tcp_init(mLoop, &mServerHandle);
        mServerHandle.data = this;
    }

    ~LibuvServer() override {
        assert(uv_is_closing((const uv_handle_t*)&mServerHandle) &&
               "LibuvServer destroyed without calling close() first!");
    }

    int port() const override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        return mPort;
    }

    void close() override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");

        if (!uv_is_closing((const uv_handle_t*)&mServerHandle)) {
            mIsListening = false;
            mServerHandle.data = new std::shared_ptr<LibuvServer>(shared_from_this());

            uv_close((uv_handle_t*)&mServerHandle, [](uv_handle_t* handle) {
                auto* self_ptr = static_cast<std::shared_ptr<LibuvServer>*>(handle->data);
                if ((*self_ptr)->mOnClose) {
                    (*self_ptr)->mOnClose();
                }
                delete self_ptr;
            });
        }
    }

    void setOnCloseCallback(AsyncSocket::OnCloseCallback cb) {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnClose = std::move(cb);
    }

    EventLoop* getLoop() const override { return mEventLoop; }

  private:
    struct sockaddr_storage bindImpl(const std::string& address) {
        auto addresses = resolveAddress(address, AI_PASSIVE);
        if (addresses.empty()) {
            LOG(ERROR) << "Failed to resolve address: " << address;
            return { .ss_family = AF_UNSPEC };
        }

        std::sort(addresses.begin(), addresses.end(),
                  [](const struct sockaddr_storage& lhs, const struct sockaddr_storage& rhs){
                      return lhs.ss_family < rhs.ss_family;
                  });

        for (auto& resolved_addr : addresses) {
            if (uv_tcp_bind(&mServerHandle, (const struct sockaddr*)&resolved_addr, 0) == 0) {
                int len = sizeof(resolved_addr);
                uv_tcp_getsockname(&mServerHandle, (sockaddr*)&resolved_addr, &len);
                return resolved_addr;
            }
        }

        LOG(ERROR) << "Failed to bind to " << address;
        return { .ss_family = AF_UNSPEC };
    }

    bool bindAndListen(const std::string& address) {
        const struct sockaddr_storage addr = bindImpl(address);
        switch (addr.ss_family) {
        case AF_INET:
            mPort = ntohs(((const sockaddr_in*)&addr)->sin_port);
            break;

        case AF_INET6:
            mPort = ntohs(((const sockaddr_in6*)&addr)->sin6_port);
            break;

        case AF_UNSPEC:
        default:
            return false;
        }

        int listen_res =
                uv_listen((uv_stream_t*)&mServerHandle, 128, [](uv_stream_t* s, int status) {
                    if (status < 0) {
                        LOG(WARNING) << "Listen error: " << UvErrToAbslStatus(status);
                        return;
                    }
                    static_cast<LibuvServer*>(s->data)->on_new_connection(s);
                });

        if (listen_res != 0) {
            LOG(ERROR) << "Failed to listen on " << address << ": "
                       << UvErrToAbslStatus(listen_res);
            return false;
        }

        mIsListening = true;
        return true;
    }

    void on_new_connection(uv_stream_t* server) {
        if (!mIsListening) return;

        auto client = std::make_shared<LibuvSocket>(mEventLoop);
        client->accept(server);

        // At this point, client.use_count() is 1.
        bool accepted = mConnectCallback(client);

        // Now, check what the user did.
        if (accepted) {
            // If the callback returned true but didn't take ownership,
            // tsk, tsk.
            if (client.use_count() == 1) {
                LOG(WARNING) << "onConnectCallback returned true but did not retain "
                             << "ownership of the socket. The connection will be closed "
                             << "to prevent it from being abandoned.";
                client->close();
            } else {
                CHECK(client->mOnRead)
                        << "`mOnRead` must be set by `mConnectCallback` to prevent loss of data.";
                client->startReading();
            }
        } else {
            client->close();
        }
    }

    EventLoop* mEventLoop;
    uv_loop_t* mLoop;
    AsyncSocket::OnCloseCallback mOnClose;
    ConnectCallback mConnectCallback;
    uv_tcp_t mServerHandle;
    int mPort = -1;
    bool mIsListening = false;
};

std::shared_ptr<LibuvServer> LibuvServer::create(EventLoop* loop, const std::string& address,
                                                 ConnectCallback cb) {
    assert(loop->isOnLoopThread() && "Factory must be used on loop thread");
    const auto server = std::make_shared<LibuvServer>(loop, std::move(cb), Private());

    if (server->bindAndListen(address)) {
        return server;
    }

    server->close();
    return nullptr;
}

// =================================================================
//          LibuvSocketFactory Implementation
// =================================================================

std::shared_ptr<AsyncSocketServer> LibuvAsyncSocketFactory::createServer(
        EventLoop* loop, const std::string& address,
        AsyncSocketServer::ConnectCallback connectCallback) {
    assert(loop->isOnLoopThread() && "Factory must be used on loop thread");
    return LibuvServer::create(loop, address, std::move(connectCallback));
}

std::shared_ptr<AsyncSocket> LibuvAsyncSocketFactory::createSocket(EventLoop* loop,
                                                                   const std::string& address) {
    assert(loop->isOnLoopThread() && "Factory must be used on loop thread");
    auto addresses = resolveAddress(address, 0);
    if (addresses.empty()) {
        LOG(ERROR) << "Failed to resolve address: " << address;
        return nullptr;
    }

    std::sort(addresses.begin(), addresses.end(),
              [](const struct sockaddr_storage& lhs, const struct sockaddr_storage& rhs){
                  return lhs.ss_family < rhs.ss_family;
              });

    // Use the first resolved address
    auto socket = std::make_shared<LibuvSocket>(loop, (const struct sockaddr*)&addresses[0]);
    return socket;
}

}  // namespace goldfish::async
