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
#include "android/base/sockets/SimpleTestServer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"

#include "aemu/base/async/AsyncSocket.h"
#include "aemu/base/async/AsyncSocketAdapter.h"
#include "aemu/base/async/DefaultLooper.h"
#include "aemu/base/async/Looper.h"
#include "aemu/base/async/ScopedSocketWatch.h"
#include "aemu/base/async/ThreadLooper.h"
#include "aemu/base/sockets/ScopedSocket.h"
#include "aemu/base/sockets/SocketUtils.h"

namespace android::base::testing {

SimpleTestServer::SimpleTestServer(int port, ReplyStrategy reply)
    : mServerSocket(android::base::socketTcp4LoopbackServer(port)),
      mReplyStrategy(std::move(reply)) {}

SimpleTestServer::~SimpleTestServer() {
    waitUntilCompleted();
    stop();
}

bool SimpleTestServer::valid() const {
    return mServerSocket.valid();
}

int SimpleTestServer::port() const {
    return android::base::socketGetPort(mServerSocket.get());
}

void SimpleTestServer::acceptConnections(int totalConnections) {
    for (int i = 0; i < totalConnections; i++) {
        int fd = android::base::socketAcceptAny(mServerSocket.get());
        if (fd < 0 || !valid()) {
            return;
        }
        mServerThreads.emplace_back(
                [this, socket = mIncomingSocket, fd] { handleConnection(fd, socket); });
        {
            std::unique_lock<std::mutex> lock(mHandlingConnectionMutex);
            mHandlingConnection = true;
            mHandlingConnectionCv.notify_all();
        }
    }
}

static void sendReply(const std::string& reply, int fd, std::shared_ptr<ChecksumSocket> socket) {
    auto buffer = reply.data();
    auto bufferSize = reply.size();
    int pos = 0;
    do {
        ssize_t ret = android::base::socketSend(fd, buffer + pos, bufferSize - pos);
        if (ret == 0) {
            errno = ECONNRESET;
            return;
        }
        pos += static_cast<size_t>(ret);
    } while (pos < bufferSize);
    socket->updateServerSend(reply);
    VLOG(1) << "Replied: " << reply;
}

void SimpleTestServer::handleConnection(int fd, std::shared_ptr<ChecksumSocket> socket) {
    ssize_t len = 0;
    char buffer[4096];
    std::string endMarkerBuffer(socket->endMarker().size(), 'x');
    do {
        auto str = std::string_view(buffer, len);
        socket->updateServerRecv(str);

        size_t overlap = std::min(endMarkerBuffer.size(), str.size());
        std::copy(str.substr(str.size() - overlap).begin(), str.substr(str.size() - overlap).end(),
                  endMarkerBuffer.begin() + (endMarkerBuffer.size() - overlap));
        if (str.size() > overlap) {
            endMarkerBuffer = std::string(str.substr(str.size() - overlap)) +
                              endMarkerBuffer.substr(0, endMarkerBuffer.size() - overlap);
        }

        auto reply = mReplyStrategy(str);
        if (!reply.empty()) {
            sendReply(reply, fd, socket);
        }
        if (endMarkerBuffer == socket->endMarker()) {
            break;
        }

        len = android::base::socketRecv(fd, buffer, sizeof(buffer));
    } while (len > 0);

    // Close the client..
    android::base::socketClose(fd);
}

void SimpleTestServer::waitUntilCompleted() {
    for (auto& connection : mServerThreads) {
        connection.join();
    }
    mServerThreads.clear();
}

void SimpleTestServer::start(int totalConnections) {
    mServerThreads.emplace_back([this, totalConnections] { acceptConnections(totalConnections); });
}

void SimpleTestServer::stop() {
    mLooper.stop();
}

std::shared_ptr<ChecksumSocket> SimpleTestServer::connect(ChecksumSocket::Mode mode) {
    mLooper.start();
    mIncomingSocket = std::make_shared<ChecksumSocket>(mLooper.looper(), port(), mode);
    mIncomingSocket->connect();
    std::unique_lock<std::mutex> lock(mHandlingConnectionMutex);
    mHandlingConnectionCv.wait(lock, [this] { return mHandlingConnection; });
    mHandlingConnection = false;
    return std::move(mIncomingSocket);
}

}  // namespace android::base::testing