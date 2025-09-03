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
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

#include "aemu/base/files/ScopedFd.h"
#include "aemu/base/sockets/SocketUtils.h"
#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/errno_to_absl.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/qemu_socket_factory.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"

// Qemu introduces a set if #defines we do not want in windows.
#ifdef _WIN32
#undef close
#undef send
#undef connect
#endif
// IWYU pragma: end_keep
// clang-format on
}

// This file provides the QEMU-based implementation of the AsyncSocketFactory.
//
// It contains the complete implementation for the factory and the internal
// socket and server classes (`QemuAsyncSocket`, `QemuAsyncSocketServer`).
// These classes are defined in an anonymous namespace to indicate that they
// are private to this compilation unit.
//
// The design consolidates all related logic into a single file to simplify
// the build configuration and improve code cohesion.
//
// A key aspect of this implementation is the use of a self-referencing
// `std::shared_ptr` to manage the lifetime of socket and server objects.
// The QEMU main loop (`qemu_set_fd_handler`) works with raw pointers, so we
// cannot rely on it to manage the lifetime of our C++ objects. Instead, the
// objects keep themselves alive by holding a shared_ptr to themselves. This
// reference is released only when the socket or server is explicitly closed,
// ensuring the object remains valid until all asynchronous operations have
// completed.
namespace goldfish::async {

namespace {

// A simple wrapper to manage the write requests.
struct QemuWriteRequest {
    std::vector<char> buffer;              ///< The data to be written.
    size_t offset;                         ///< Current write offset within the buffer.
    AsyncSocket::OnSendCallback callback;  ///< Callback to be invoked on completion.
};

// An implementation of AsyncSocket that uses the QEMU main loop for
// asynchronous I/O.
//
// This class manages its own lifetime using a self-referencing shared_ptr
// (`mSelfShared`). The `start()` method captures a shared_ptr to `this`,
// which is held until `close()` is called. This is necessary because the
// QEMU `qemu_set_fd_handler` only stores a raw pointer. The self-reference
// ensures the object is not destroyed while it's still being used by the
// QEMU main loop.
//
// The `onRead` and `onWrite` methods are the core of the I/O handling:
// - `onRead`: This handler is called by the QEMU main loop when the socket's
//   file descriptor becomes readable. It reads the available data and passes
//   it to the user through the `OnReadCallback`. It also handles the case
//   where the peer closes the connection (a read of 0 bytes) and socket
//   errors, both of which result in the socket being closed.
// - `onWrite`: This handler is called when the socket is writable. It works
//   through a queue of pending write requests (`mWriteQueue`). For each
//   request, it attempts to send as much data as the socket will accept.
//   Once a request is completely sent, its corresponding callback is invoked.
//   The socket is only registered for write notifications if this queue is
//   not empty, preventing unnecessary wakeups.
class QemuAsyncSocket : public AsyncSocket, public std::enable_shared_from_this<QemuAsyncSocket> {
  public:
    QemuAsyncSocket(EventLoop* loop, android::base::ScopedFd fd) : mLoop(loop), mFd(std::move(fd)) {
        android::base::socketSetNonBlocking(mFd.get());
    }

    ~QemuAsyncSocket() override {
        if (mFd.valid()) {
            // This indicates close() was not called, which is a bug.
            LOG(WARNING) << "QemuAsyncSocket destroyed without being closed.";
            close();
        }
    }

    // AsyncSocket interface
    void setOnReadCallback(OnReadCallback cb) override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnRead = std::move(cb);
    }
    void setOnCloseCallback(OnCloseCallback cb) override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnClose = std::move(cb);
    }
    void setOnConnectedCallback(OnConnectCallback cb) override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        mOnConnect = std::move(cb);
    }

    absl::Status send(const char* buffer, size_t bufferSize, OnSendCallback cb) override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        if (!mFd.valid()) {
            return absl::InternalError("Socket is not connected.");
        }
        mWriteQueue.push_back({std::vector<char>(buffer, buffer + bufferSize), 0, std::move(cb)});

        // If we aren't watching for writes, start now.
        if (!mIsWatchingWrite) {
            mIsWatchingWrite = true;
            qemu_set_fd_handler(mFd.get(), &QemuAsyncSocket::static_onRead,
                                &QemuAsyncSocket::static_onWrite, this);
        }
        return absl::OkStatus();
    }

    void close() override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        if (!mFd.valid()) {
            VLOG(1) << "Closing an already closed socket.";
            return;
        }
        // Remove all handlers for this fd
        qemu_set_fd_handler(mFd.get(), nullptr, nullptr, nullptr);

        mFd.close();
        if (mOnClose) {
            mOnClose();
        }
        // Release the self-shared_ptr to allow destruction.
        mSelfShared.reset();
    }

    absl::Status connect() override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        // The socket is created connected by the factory.
        // We just need to notify the user.
        mLoop->post([self = shared_from_this()]() {
            if (self->mOnConnect) {
                self->mOnConnect(absl::OkStatus());
            }
        });
        return absl::OkStatus();
    }

    bool connected() const override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        return mFd.valid();
    }
    EventLoop* getLoop() const override { return mLoop; }

    void start() {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        mSelfShared = shared_from_this();
        // Start by watching for reads only.
        qemu_set_fd_handler(mFd.get(), &QemuAsyncSocket::static_onRead, nullptr, this);
    }

  private:
    // Static handlers to be passed to qemu_set_fd_handler
    static void static_onRead(void* opaque) { static_cast<QemuAsyncSocket*>(opaque)->onRead(); }
    static void static_onWrite(void* opaque) { static_cast<QemuAsyncSocket*>(opaque)->onWrite(); }

    void onRead() {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        char buffer[4096];
        ssize_t bytesRead = android::base::socketRecv(mFd.get(), buffer, sizeof(buffer));
        VLOG(1) << "Read: " << bytesRead << ", from: " << mFd.get() << ", errno: " << errno;
        if (bytesRead > 0) {
            if (mOnRead) {
                mOnRead({buffer, (size_t)bytesRead}, absl::OkStatus());
            }
        } else if (bytesRead == 0) {
            // Peer closed the connection.
            close();

        } else {
            // Error
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (mOnRead) {
                    mOnRead({}, ErrnoToAbslStatus(errno));
                }
                close();
            }
        }
    }

    void onWrite() {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        if (mWriteQueue.empty()) {
            // This should not be reached, but as a safeguard.
            if (mIsWatchingWrite) {
                mIsWatchingWrite = false;
                qemu_set_fd_handler(mFd.get(), &QemuAsyncSocket::static_onRead, nullptr, this);
            }
            return;
        }

        auto& req = mWriteQueue.front();
        ssize_t bytesSent = android::base::socketSend(mFd.get(), req.buffer.data() + req.offset,
                                                      req.buffer.size() - req.offset);

        VLOG(1) << "Send " << bytesSent << " to " << mFd.get();
        if (bytesSent >= 0) {
            req.offset += bytesSent;
            if (req.offset == req.buffer.size()) {
                // Full send
                if (req.callback) {
                    req.callback(absl::OkStatus());
                }
                mWriteQueue.pop_front();
            }
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // Error
                if (req.callback) {
                    req.callback(ErrnoToAbslStatus(errno));
                }
                mWriteQueue.pop_front();  // Discard this write.
            }
        }

        if (mWriteQueue.empty()) {
            if (mIsWatchingWrite) {
                mIsWatchingWrite = false;
                qemu_set_fd_handler(mFd.get(), &QemuAsyncSocket::static_onRead, nullptr, this);
            }
        }
    }

    EventLoop* mLoop;
    android::base::ScopedFd mFd;
    bool mIsWatchingWrite = false;
    AsyncSocket::OnReadCallback mOnRead;
    AsyncSocket::OnCloseCallback mOnClose;
    AsyncSocket::OnConnectCallback mOnConnect;
    std::deque<QemuWriteRequest> mWriteQueue;
    std::shared_ptr<QemuAsyncSocket> mSelfShared;
};

// An implementation of AsyncSocketServer that uses the QEMU main loop to
// accept incoming connections.
//
// Like QemuAsyncSocket, this class manages its own lifetime using a
// self-referencing shared_ptr (`mSelf`). The `start()` method captures a
// shared_ptr to `this`, which is held until `close()` is called. This
// ensures the server object remains alive and can accept connections until
// it is explicitly shut down.
//
// Note that this server socket only binds to localhost.
class QemuAsyncSocketServer : public AsyncSocketServer,
                              public std::enable_shared_from_this<QemuAsyncSocketServer> {
  public:
    QemuAsyncSocketServer(EventLoop* loop, const ConnectCallback onConnect,
                          const std::string& listenOn)
            : mLoop(loop), mOnConnect(onConnect) {
        size_t last_colon = listenOn.find_last_of(':');
        if (last_colon == std::string::npos) {
            LOG(ERROR) << "Invalid listenOn format: " << listenOn;
            return;
        }
        std::string ip_str = listenOn.substr(0, last_colon);
        int port;
        if (!absl::SimpleAtoi(listenOn.substr(last_colon + 1), &port)) {
            LOG(ERROR) << "Invalid port in listenOn: " << listenOn;
            return;
        }

        if (ip_str != "127.0.0.1" && ip_str != "localhost" && ip_str != "::1") {
            LOG(ERROR) << "QemuAsyncSocketServer only supports loopback connections, not "
                       << ip_str;
            return;
        }

        int fd = android::base::socketTcp4LoopbackServer(port);
        if (fd < 0) {
            // Try
            fd = android::base::socketTcp6LoopbackServer(port);
            if (fd < 0) {
                LOG(ERROR) << "Failed to create server socket: " << ErrnoToAbslStatus(-fd);
                return;
            }
        }
        mListenFd = android::base::ScopedFd(fd);
        mPort = android::base::socketGetPort(mListenFd.get());
    }

    ~QemuAsyncSocketServer() override {
        if (mListenFd.valid()) {
            LOG(WARNING) << "QemuAsyncSocketServer destroyed without being closed.";
            close();
        }
    }

    int port() const override { return mPort; }

    void close() override {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        if (mListenFd.valid()) {
            qemu_set_fd_handler(mListenFd.get(), nullptr, nullptr, nullptr);
            mListenFd.close();
        }
        mSelf.reset();
    }

    EventLoop* getLoop() const override { return mLoop; }

    void start() {
        assert(mLoop->isOnLoopThread() && "Must be called on loop thread");
        mSelf = shared_from_this();
        qemu_set_fd_handler(mListenFd.get(), &QemuAsyncSocketServer::static_onAccept, nullptr,
                            this);
    }

  private:
    static void static_onAccept(void* opaque) {
        static_cast<QemuAsyncSocketServer*>(opaque)->onAccept();
    }

    void onAccept() {
        VLOG(1) << "Accepting socket";
        auto clientFd = android::base::ScopedFd(android::base::socketAcceptAny(mListenFd.get()));
        if (!clientFd.valid()) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG(ERROR) << "Failed to accept connection: " << ErrnoToAbslStatus(errno);
            }
            return;
        }

        auto socket = std::make_shared<QemuAsyncSocket>(mLoop, std::move(clientFd));
        socket->start();

        if (mOnConnect) {
            if (!mOnConnect(socket)) {
                // The user rejected the connection.
                socket->close();
            }
        }
    }

    EventLoop* mLoop;
    ConnectCallback mOnConnect;
    android::base::ScopedFd mListenFd;
    int mPort = -1;
    std::shared_ptr<QemuAsyncSocketServer> mSelf;
};

}  // namespace

std::shared_ptr<AsyncSocket> QemuSocketFactory::createSocket(EventLoop* loop,
                                                             const std::string& address) {
    size_t last_colon = address.find_last_of(':');
    if (last_colon == std::string::npos) {
        LOG(ERROR) << "Invalid address format: " << address;
        return nullptr;
    }
    std::string ip_str = address.substr(0, last_colon);
    int port;
    if (!absl::SimpleAtoi(address.substr(last_colon + 1), &port)) {
        LOG(ERROR) << "Invalid port in address: " << address;
        return nullptr;
    }

    // For now, we only support loopback connections for qemu sockets in tests.
    if (ip_str != "127.0.0.1" && ip_str != "localhost" && ip_str != "::1") {
        LOG(ERROR) << "QemuSocketFactory only supports loopback connections, not " << ip_str;
        return nullptr;
    }

    int fd = android::base::socketTcp4LoopbackClient(port);
    if (fd < 0) {
        fd = android::base::socketTcp6LoopbackClient(port);
        if (fd < 0) {
            // Note: socketTcp4LoopbackClient returns -errno on failure.
            LOG(ERROR) << "Failed to connect to " << address << ": " << ErrnoToAbslStatus(-fd);
            return nullptr;
        }
    }

    auto socket = std::make_shared<QemuAsyncSocket>(loop, android::base::ScopedFd(fd));
    socket->start();
    return socket;
}

std::shared_ptr<AsyncSocketServer> QemuSocketFactory::createServer(
        EventLoop* loop, const std::string& listenOn,
        const AsyncSocketServer::ConnectCallback onConnect) {
    auto server = std::make_shared<QemuAsyncSocketServer>(loop, onConnect, listenOn);
    if (server->port() == -1) {
        return nullptr;
    }
    server->start();
    return server;
}

}  // namespace goldfish::async
