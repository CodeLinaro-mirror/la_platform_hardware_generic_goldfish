/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "goldfish/devices/marshalling_hal_socket.h"

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::devices {

namespace {

// A socket that sends things nowhere..
struct NullSocket : public cable::ISocket {
    ~NullSocket() override = default;
    void SendAsync(const void* /*data*/, size_t size) override {
        VLOG(2) << "Sending " << size << " bytes to /dev/null";
    };
    cable::PlugPtr SwitchPlug(cable::PlugPtr /*new_plug*/) override { return {}; }
    cable::PlugPtr UnplugImpl() override {
        VLOG(1) << "Unplugging the NullSocket";
        return {};
    }
    void AbslStringifyImpl(absl::FormatSink& s) const override { absl::Format(&s, "[NullSocket]"); }
};

NullSocket g_null_socket;
}  // namespace

MarshallingHalSocket::MarshallingHalSocket(cable::SocketPtr socket, async::EventLoop* qemu_loop)
        : socket_(std::move(socket)), qemu_loop_(qemu_loop) {
    VLOG(1) << "MarshallingHalSocket: " << socket_ << " created";
}

MarshallingHalSocket::~MarshallingHalSocket() {
    VLOG(1) << "~MarshallingHalSocket: " << socket_ << " destroyed";
    if (!is_closed_) {
        LOG(WARNING) << "Inner socket was not closed!";
    }
}

void MarshallingHalSocket::Send(std::string data) {
    if (is_closed_) {
        VLOG(2) << "Dropping packet, socket is closed.";
        return;
    }

    VLOG(2) << "Sheduling send for " << data.size() << " bytes";
    // Post the send operation to the QEMU loop asynchronously.
    qemu_loop_->Post([this, data = std::move(data), self = shared_from_this()]() {
        const absl::MutexLock lock(&socket_mutex_);
        VLOG(2) << "Sending " << data.size() << " bytes";
        // Bytes go either to the *real* or NullSocket..
        socket_->SendAsync(data.data(), data.size());
    });
}

void MarshallingHalSocket::AbslStringifyImpl(absl::FormatSink& s) const {
    absl::Format(&s, "[MarshallingHalSocket %s, %v]", is_closed_ ? "closed" : "open", *socket_);
}

cable::SocketPtr MarshallingHalSocket::Release() {
    const absl::MutexLock lock(&socket_mutex_);
    VLOG(1) << "Releasing the socket.";
    auto s = std::move(socket_);
    socket_ = cable::SocketPtr(&g_null_socket);
    return s;
}

void MarshallingHalSocket::Close() {
    if (!is_closed_.exchange(true)) {
        VLOG(1) << "Closing the socket.";
        // Post the unplug operation to the QEMU loop asynchronously.
        // This avoids deadlocking if close() is called from a client
        // callback that was initiated by the QEMU loop.
        qemu_loop_->Post([this, self = shared_from_this()]() {
            VLOG(1) << "Calling onplug on socket";
            cable::SocketPtr socket_to_unplug;
            {
                // Safely take ownership of the real socket pointer
                // under the lock. This coordinates with the release()
                // method, which may be called by onUnplug on this same
                // QEMU thread.
                const absl::MutexLock lock(&socket_mutex_);
                socket_to_unplug = std::move(socket_);

                VLOG(1) << "Installing null socket, welcome to the void.";
                socket_ = cable::SocketPtr(&g_null_socket);
            }

            // Unplug the real socket outside the lock.
            // Don't unplug if it was already the null socket (e.g., if
            // release() was called first).
            if (socket_to_unplug && socket_to_unplug.get() != &g_null_socket) {
                VLOG(1) << "Unplugging the real socket.";
                cable::ISocket::Unplug(std::move(socket_to_unplug));
            }
        });
    } else {
        VLOG(1) << "Socket already closed";
    }
}
}  // namespace goldfish::devices
