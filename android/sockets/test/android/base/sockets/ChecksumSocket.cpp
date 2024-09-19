// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/base/sockets/ChecksumSocket.h"

#include <chrono>
#include <mutex>
#include <string_view>

#include "absl/log/log.h"

namespace android::base::testing {
ChecksumSocket::ChecksumSocket(android::base::Looper* looper, int port, Mode mode)
    : mSocket(looper, port), mMode(mode) {
    mSocket.setSocketEventListener(this);
}

bool ChecksumSocket::connect() {
    return mSocket.connectSync(std::chrono::milliseconds(100));
}

void ChecksumSocket::send(std::string_view bytes) {
    {
        std::unique_lock<std::mutex> lock(mChecksumAccess);
        std::get<0>(mSend).update(bytes);
    }

    if (mMode == Mode::StoreAll) {
        std::unique_lock<std::mutex> lock(mStringAccess);
        std::get<0>(mSendStr).append(bytes);
    }

    VLOG(1) << "Sending " << bytes;
    mSocket.send(bytes.data(), bytes.size());
}

void ChecksumSocket::end() {
    send(kEND_MARKER);
}

bool ChecksumSocket::verifySend() {
    std::unique_lock<std::mutex> lock(mChecksumAccess);
    const auto& [client, server] = mSend;
    return client.getChecksum() == server.getChecksum();
}

bool ChecksumSocket::verifyRecv() {
    std::unique_lock<std::mutex> lock(mChecksumAccess);
    const auto& [client, server] = mRecv;
    return client.getChecksum() == server.getChecksum();
}

void ChecksumSocket::updateServerRecv(std::string_view bytes) {
    VLOG(1) << "Server recv: " << bytes;
    {
        std::unique_lock<std::mutex> lock(mChecksumAccess);
        auto& [client, server] = mSend;
        server.update(bytes);
    }
    if (mMode == Mode::StoreAll) {
        std::unique_lock<std::mutex> lock(mStringAccess);
        auto& [_, serverStr] = mSendStr;
        serverStr.append(bytes);
    }
}

void ChecksumSocket::updateServerSend(std::string_view bytes) {
    VLOG(1) << "Server send: " << bytes;
    {
        std::unique_lock<std::mutex> lock(mChecksumAccess);
        auto& [client, server] = mRecv;
        server.update(bytes);
    }
    if (mMode == Mode::StoreAll) {
        std::unique_lock<std::mutex> lock(mStringAccess);
        auto& [_, serverStr] = mRecvStr;
        serverStr.append(bytes);
    }
}

std::string_view ChecksumSocket::endMarker() {
    return kEND_MARKER;
}

void ChecksumSocket::onRead(android::base::AsyncSocketAdapter* socket) {
    char buffer[8192];
    do {
        int bytes = mSocket.recv(buffer, sizeof(buffer));
        if (bytes <= 0) {
            break;
        }
        {
            std::unique_lock<std::mutex> lock(mChecksumAccess);
            std::get<0>(mRecv).update(std::string_view(buffer, bytes));
        }
        if (mMode == Mode::StoreAll) {
            std::unique_lock<std::mutex> lock(mChecksumAccess);
            std::get<0>(mRecvStr).append(buffer, bytes);
        }
    } while (true);
}

void ChecksumSocket::onClose(android::base::AsyncSocketAdapter* socket, int err) {
    if (mOnClose) {
        mOnClose();
    };
}

void ChecksumSocket::onConnected(android::base::AsyncSocketAdapter* socket) {}
}  // namespace android::base::testing