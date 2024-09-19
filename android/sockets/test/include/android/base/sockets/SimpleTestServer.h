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
#include <memory>
#include <tuple>

#include "absl/log/log.h"

#include "aemu/base/sockets/ScopedSocket.h"
#include "aemu/base/sockets/SocketUtils.h"
#include "android/base/sockets/ChecksumCalculator.h"
#include "android/base/sockets/ChecksumSocket.h"
#include "android/base/sockets/SimpleThreadLooper.h"

namespace android::base::testing {
using android::base::AsyncSocketAdapter;
using android::base::Looper;
using android::base::ScopedSocket;

using ReplyStrategy = std::function<std::string(std::string_view)>;

/**
 * @brief A simple test server for network communication testing.
 */
class SimpleTestServer {
  public:
    /**
     * @brief Constructs a SimpleTestServer.
     * @param port The port to bind to (0 for automatic port selection).
     * @param reply The reply strategy to use (optional).
     */
    SimpleTestServer(int port = 0, ReplyStrategy reply = [](auto _) { return ""; });

    /**
     * @brief Destructor. Waits for completion and stops the server.
     */
    ~SimpleTestServer();

    /**
     * @brief Checks if the server is valid (port bound successfully).
     * @return True if valid, false otherwise.
     */
    bool valid() const;

    /**
     * @brief Gets the bound server port.
     * @return The server port.
     */
    int port() const;

    /**
     * @brief Accepts a specified number of connections.
     * @param totalConnections The number of connections to accept.
     */
    void acceptConnections(int totalConnections);

    /**
     * @brief Handles a single connection.
     * @param fd The file descriptor of the connection.
     * @param socket The ChecksumSocket associated with the connection.
     */
    void handleConnection(int fd, std::shared_ptr<ChecksumSocket> socket);

    /**
     * @brief Waits for all connections to complete.
     */
    void waitUntilCompleted();

    /**
     * @brief Starts the server and accepts connections.
     * @param totalConnections The number of connections to accept (default 1).
     */
    void start(int totalConnections = 1);

    /**
     * @brief Stops the server.
     */
    void stop();

    /**
     * @brief Connects to the server and returns a ChecksumSocket.
     * @return A shared pointer to the connected ChecksumSocket.
     */
    std::shared_ptr<ChecksumSocket> connect(
            ChecksumSocket::Mode mode = ChecksumSocket::Mode::StoreAll);

  private:
    bool mHandlingConnection{false};
    std::shared_ptr<ChecksumSocket> mIncomingSocket;
    std::mutex mHandlingConnectionMutex;
    std::mutex mCreateConnection;
    std::condition_variable mHandlingConnectionCv;

    ScopedSocket mServerSocket;
    std::vector<std::thread> mServerThreads;
    SimpleThreadLooper mLooper;
    ReplyStrategy mReplyStrategy;
};

}  // namespace android::base::testing
