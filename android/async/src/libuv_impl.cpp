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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/uv_to_absl.h"

namespace goldfish::async {

class LibuvSocket;

struct LibuvSocketContext {
    LibuvSocket* self;
};

struct write_req_t {
    uv_write_t req;
    uv_buf_t buf;
    AsyncSocket::OnSendCallback cb;
};

// =================================================================
//                 CONCRETE IMPLEMENTATION CLASSES
// =================================================================

class LibuvSocket : public AsyncSocket, public std::enable_shared_from_this<LibuvSocket> {
  public:
    explicit LibuvSocket(EventLoop* loop)
            : mEventLoop(loop), mLoop(static_cast<uv_loop_t*>(loop->getRawLoop())) {
        tcpInit();
    }

    LibuvSocket(EventLoop* loop, const struct sockaddr* addr)
            : mEventLoop(loop), mLoop(static_cast<uv_loop_t*>(loop->getRawLoop())) {
        mAddr = *reinterpret_cast<const sockaddr_storage*>(addr);
        tcpInit();
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

        auto* writeReq = new write_req_t();
        char* write_buffer = new char[bufferSize];
        memcpy(write_buffer, buffer, bufferSize);
        writeReq->buf = uv_buf_init(write_buffer, bufferSize);
        writeReq->cb = std::move(cb);

        uv_write(&writeReq->req, (uv_stream_t*)&mTcpHandle, &writeReq->buf, 1,
                 [](uv_write_t* req, int s) {
                     auto* w = reinterpret_cast<write_req_t*>(req);
                     w->cb(UvErrToAbslStatus(s));
                     delete[] w->buf.base;
                     delete w;
                 });
        return absl::OkStatus();
    }

    void close() override {
        assert(mEventLoop->isOnLoopThread() && "Must be called on loop thread");
        mIsConnected = false;

        if (!uv_is_closing((const uv_handle_t*)&mTcpHandle)) {
            delete mTcpHandle.data;
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

  private:
    friend class LibuvServer;

    void tcpInit() {
        uv_tcp_init(mLoop, &mTcpHandle);
        mTcpHandle.data = new LibuvSocketContext{.self = this};
    }

    void startReading() {
        assert(mEventLoop->isOnLoopThread());
        if (uv_is_closing((const uv_handle_t*)&mTcpHandle)) return;

        uv_read_start((uv_stream_t*)&mTcpHandle,
                      [](uv_handle_t*, size_t size, uv_buf_t* buf) {
                          *buf = uv_buf_init(new char[size], size);
                      },
                      [](uv_stream_t* s, ssize_t n, const uv_buf_t* b) {
                          auto* ctx = static_cast<LibuvSocketContext*>(s->data);
                          ctx->self->on_read(n, b);
                          delete[] b->base;
                      });
    }

    void accept(uv_stream_t* server_handle) {
        assert(mEventLoop->isOnLoopThread());
        auto result = uv_accept(server_handle, (uv_stream_t*)&mTcpHandle);
        VLOG(1) << "accept: " << UvErrToAbslStatus(result);
        if (result == 0) {
            mIsConnected = true;
        } else {
            LOG(WARNING) << "Failed to accept incoming connection: " << uv_strerror(result);
            uv_close((uv_handle_t*)&mTcpHandle, nullptr);
        }
    }

    void on_read(ssize_t nread, const uv_buf_t* buf) {
        VLOG(1) << "on_read: " << nread << " : " << UvErrToAbslStatus(nread);
        if (nread >= 0) {
            // Success path (nread > 0) or no-op (nread == 0).
            if (mOnRead) {
                mOnRead({buf->base, (size_t)nread}, absl::OkStatus());
            }
        } else {
            // Error path (nread < 0). This is a fatal, unrecoverable stream error.
            if (mOnRead) {
                mOnRead({}, UvErrToAbslStatus(nread));
            }
            close();
        }
    }

    void on_connect(int status) {
        VLOG(1) << "on_connect: " << UvErrToAbslStatus(status);
        if (mOnConnected) {
            if (status == 0) {
                mIsConnected = true;
                mOnConnected(absl::OkStatus());
                startReading();
            } else {
                // Failure case
                mOnConnected(UvErrToAbslStatus(status));
            }
        }
    }

    EventLoop* mEventLoop;
    uv_loop_t* mLoop;
    uv_tcp_t mTcpHandle;
    sockaddr_storage mAddr;
    bool mIsConnected = false;

    OnReadCallback mOnRead;
    OnCloseCallback mOnClose;
    OnConnectCallback mOnConnected;
};

class LibuvServer : public AsyncSocketServer, public std::enable_shared_from_this<LibuvServer> {
  public:
    LibuvServer(EventLoop* loop, const std::string& address, ConnectCallback cb)
            : mEventLoop(loop)
            , mLoop(static_cast<uv_loop_t*>(loop->getRawLoop()))
            , mConnectCallback(std::move(cb))
            , mPort(-1)
            , mIsListening(false) {
        assert(mEventLoop->isOnLoopThread() && "Must be constructed on loop thread");

        struct sockaddr_storage addr;
        size_t c = address.find_last_of(':');
        std::string ip = address.substr(0, c);
        int p = std::stoi(address.substr(c + 1));
        if (uv_ip4_addr(ip.c_str(), p, (sockaddr_in*)&addr) != 0 &&
            uv_ip6_addr(ip.c_str(), p, (sockaddr_in6*)&addr) != 0)
            return;

        uv_tcp_init(mLoop, &mServerHandle);
        mServerHandle.data = this;
        if (uv_tcp_bind(&mServerHandle, (const sockaddr*)&addr, 0) != 0) return;

        uv_listen((uv_stream_t*)&mServerHandle, 128, [](uv_stream_t* s, int status) {
            if (status < 0) return;
            static_cast<LibuvServer*>(s->data)->on_new_connection(s);
        });

        mIsListening = true;
        int len = sizeof(addr);
        uv_tcp_getsockname(&mServerHandle, (sockaddr*)&addr, &len);
        mPort = ntohs(addr.ss_family == AF_INET ? ((sockaddr_in*)&addr)->sin_port
                                                : ((sockaddr_in6*)&addr)->sin6_port);
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
    int mPort;
    bool mIsListening;
};

// =================================================================
//          LibuvSocketFactory Implementation
// =================================================================

std::shared_ptr<AsyncSocketServer> LibuvAsyncSocketFactory::createServer(
        EventLoop* loop, const std::string& address,
        AsyncSocketServer::ConnectCallback connectCallback) {
    assert(loop->isOnLoopThread() && "Factory must be used on loop thread");
    auto server = std::make_shared<LibuvServer>(loop, address, std::move(connectCallback));
    if (server->port() != -1) {
        return server;
    }
    return nullptr;
}

std::shared_ptr<AsyncSocket> LibuvAsyncSocketFactory::createSocket(EventLoop* loop,
                                                                   const std::string& address) {
    assert(loop->isOnLoopThread() && "Factory must be used on loop thread");
    struct sockaddr_storage addr;
    size_t last_colon = address.find_last_of(':');
    std::string ip_str = address.substr(0, last_colon);
    int port = std::stoi(address.substr(last_colon + 1));
    if (uv_ip4_addr(ip_str.c_str(), port, (sockaddr_in*)&addr) != 0 &&
        uv_ip6_addr(ip_str.c_str(), port, (sockaddr_in6*)&addr) != 0) {
        return nullptr;
    }
    return std::make_shared<LibuvSocket>(loop, (const struct sockaddr*)&addr);
}

}  // namespace goldfish::async