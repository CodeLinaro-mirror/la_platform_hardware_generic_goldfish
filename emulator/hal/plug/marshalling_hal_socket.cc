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

namespace goldfish {
namespace devices {

namespace {

// A socket that sends things nowhere..
struct NullSocket : public cable::ISocket {
    ~NullSocket() override {}
    void sendAsync(const void* data, size_t size) override {
        VLOG(2) << "Sending " << size << " bytes to /dev/null";
    };
    cable::PlugPtr switchPlug(cable::PlugPtr newPlug) override { return {}; }
    cable::PlugPtr unplugImpl() override {
        VLOG(1) << "Unplugging the NullSocket";
        return {};
    }
    void AbslStringifyImpl(absl::FormatSink& s) const override { absl::Format(&s, "[NullSocket]"); }
};

NullSocket gNullSocket;
}  // namespace

MarshallingHalSocket::MarshallingHalSocket(cable::SocketPtr socket, async::EventLoop* qemuLoop)
        : mSocket(std::move(socket)), mQemuLoop(qemuLoop) {
    VLOG(1) << "MarshallingHalSocket: " << mSocket << " created";
}

MarshallingHalSocket::~MarshallingHalSocket() {
    VLOG(1) << "~MarshallingHalSocket: " << mSocket << " destroyed";
    if (!mIsClosed) {
        LOG(WARNING) << "Inner socket was not closed!";
    }
}

void MarshallingHalSocket::send(std::string data) {
    if (mIsClosed) {
        VLOG(2) << "Dropping packet, socket is closed.";
        return;
    }

    VLOG(2) << "Sheduling send for " << data.size() << " bytes";
    // Post the send operation to the QEMU loop asynchronously.
    mQemuLoop->post([this, data = std::move(data), self = shared_from_this()]() {
        absl::MutexLock lock(&mSocketMutex);
        VLOG(2) << "Sending " << data.size() << " bytes";
        // Bytes go either to the *real* or NullSocket..
        mSocket->sendAsync(data.data(), data.size());
    });
}

void MarshallingHalSocket::AbslStringifyImpl(absl::FormatSink& s) const {
    absl::Format(&s, "[MarshallingHalSocket %s, %v]", mIsClosed ? "closed" : "open", *mSocket);
}

cable::SocketPtr MarshallingHalSocket::release() {
    absl::MutexLock lock(&mSocketMutex);
    VLOG(1) << "Releasing the socket.";
    auto s = std::move(mSocket);
    mSocket = cable::SocketPtr(&gNullSocket);
    return s;
}

void MarshallingHalSocket::close() {
    if (!mIsClosed.exchange(true)) {
        VLOG(1) << "Closing the socket.";
        // Post the unplug operation to the QEMU loop asynchronously.
        // This avoids deadlocking if close() is called from a client
        // callback that was initiated by the QEMU loop.
        mQemuLoop->post([this, self = shared_from_this()]() {
            VLOG(1) << "Calling onplug on socket";
            cable::SocketPtr socketToUnplug;
            {
                // Safely take ownership of the real socket pointer
                // under the lock. This coordinates with the release()
                // method, which may be called by onUnplug on this same
                // QEMU thread.
                absl::MutexLock lock(&mSocketMutex);
                socketToUnplug = std::move(mSocket);

                VLOG(1) << "Installing null socket, welcome to the void.";
                mSocket = cable::SocketPtr(&gNullSocket);
            }

            // Unplug the real socket outside the lock.
            // Don't unplug if it was already the null socket (e.g., if
            // release() was called first).
            if (socketToUnplug && socketToUnplug.get() != &gNullSocket) {
                VLOG(1) << "Unplugging the real socket.";
                cable::ISocket::unplug(std::move(socketToUnplug));
            }
        });
    } else {
        VLOG(1) << "Socket already closed";
    }
}
}  // namespace devices
}  // namespace goldfish