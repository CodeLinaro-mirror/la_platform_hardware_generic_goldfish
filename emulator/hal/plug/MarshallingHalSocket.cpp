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
#include "goldfish/hal/plug/MarshallingHalSocket.h"

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace goldfish {
namespace devices {

namespace {

// A socket that sends things nowhere..
struct NullSocket : public cable::ISocket {
    ~NullSocket() override { VLOG(1) << "Program exit!"; }
    void sendAsync(const void* data, size_t size) override {};
    cable::PlugPtr switchPlug(cable::PlugPtr newPlug) override { return {}; }
    cable::PlugPtr unplugImpl() override { return {}; }
};

NullSocket gNullSocket;
}  // namespace

MarshallingHalSocket::MarshallingHalSocket(cable::SocketPtr socket, async::EventLoop* qemuLoop)
        : mSocket(std::move(socket)), mQemuLoop(qemuLoop) {
    VLOG(1) << "MarshallingHalSocket: " << mSocket << " created";
}

MarshallingHalSocket::~MarshallingHalSocket() {
    if (!mIsClosed) {
        VLOG(1) << "Inner socket was not closed, closing it now";
        close();
    }
}

void MarshallingHalSocket::send(std::string data) {
    if (mIsClosed) return;

    mQemuLoop->post([this, data = std::move(data)]() {
        VLOG(1) << "Sending " << data.size() << " bytes";
        // Bytes go either to the *real* or NullSocket..
        mSocket->sendAsync(data.data(), data.size());
    });
}

void MarshallingHalSocket::close() {
    if (!mIsClosed.exchange(true)) {
        // Note that we install a Nullsocket, so a client that has posted
        // a send event on the qemu loop as well has 2 options:
        // 1. It gets executed before us, bytes go to socket
        // 2. It gets executed after us, bytes go nowhere.
        //
        // We need to block and wait as we must make sure we do not get
        // into a non-deterministic state with regards to our own lifetime.
        mQemuLoop->postAndWait([this]() mutable {
            VLOG(1) << "Installing the Null socket and closing up: " << mSocket;
            auto s = std::move(mSocket);
            mSocket = cable::SocketPtr(&gNullSocket);
            cable::ISocket::unplug(std::move(s));
        });
    }
}
}  // namespace devices
}  // namespace goldfish
