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
#pragma once
#include <functional>
#include <string_view>
#include <tuple>

#include "aemu/base/async/AsyncSocket.h"
#include "android/base/sockets/ChecksumCalculator.h"

namespace android::base::testing {

/**
 * @brief A socket implementation that provides checksum verification for both sent and received
 * data.
 *
 * This class extends the `android::base::AsyncSocketEventListener` to handle asynchronous socket
 * events. It maintains two checksum calculators for each direction (send and receive) to ensure
 * data integrity.
 */
class ChecksumSocket : public android::base::AsyncSocketEventListener {
  public:
    using OnCloseCallback = std::function<void()>;

    enum class Mode {
        Checksum,
        StoreAll,
    };

    /** @brief Marker used to signal the end of data transmission. */
    static constexpr std::string_view kEND_MARKER{"!!FINISHED!!"};

    /**
     * @brief Constructs a ChecksumSocket.
     * @param looper The Looper associated with the socket.
     * @param port The port to connect to.
     */
    ChecksumSocket(android::base::Looper* looper, int port, Mode mode = Mode::Checksum);

    void setOnCloseCallback(OnCloseCallback callback) { mOnClose = std::move(callback); }

    /**
     * @brief The raw underlying socket.
     * @return The AsyncSocket used for sending and receiving.
     */

    android::base::AsyncSocket* raw() { return &mSocket; }

    /**
     * @brief Attempts to connect the socket synchronously.
     * @return True if the connection is successful, false otherwise.
     */
    bool connect();

    /**
     * @brief Sends data over the socket and updates the send checksum.
     * @param bytes The data to send.
     */
    void send(std::string_view bytes);

    /**
     * @brief Sends the end marker.
     */
    void end();

    /**
     * @brief Verifies the checksum for sent data.
     * @return True if the client and server checksums match, false otherwise.
     */
    bool verifySend();

    /**
     * @brief Verifies the checksum for received data.
     * @return True if the client and server checksums match, false otherwise.
     */
    bool verifyRecv();

    /**
     * @brief Updates the server's receive checksum.
     * @param bytes The received data.
     */
    void updateServerRecv(std::string_view bytes);

    /**
     * @brief Updates the server's send checksum.
     * @param bytes The data sent by the server.
     */
    void updateServerSend(std::string_view bytes);

    /**
     * @brief Returns the end marker.
     * @return The end marker string view.
     */
    std::string_view endMarker();

    // Overrides from AsyncSocketEventListener
    /**
     * @brief Handles data received on the socket.
     * @param socket The socket on which data was received.
     */
    void onRead(android::base::AsyncSocketAdapter* socket) override;

    /**
     * @brief Handles socket closure.
     * @param socket The socket that was closed.
     * @param err The error code associated with the closure.
     */
    void onClose(android::base::AsyncSocketAdapter* socket, int err) override;

    /**
     * @brief Handles successful socket connection.
     * @param socket The socket that was connected.
     */
    void onConnected(android::base::AsyncSocketAdapter* socket) override;

    std::tuple<std::string, std::string> sendStrings() { return mSendStr; }

    std::tuple<std::string, std::string> recvStrings() { return mRecvStr; }

  private:
    /** @brief The underlying AsyncSocket. */
    android::base::AsyncSocket mSocket;

    /** @brief Checksum calculators for sent data. <0> = Client, <1> = Server Recv */
    std::tuple<ChecksumCalculator, ChecksumCalculator> mSend;

    /** @brief Raw strings for sent data. <0> = Client, <1> = Server Recv */
    std::tuple<std::string, std::string> mSendStr;

    /** @brief Checksum calculators for received data. <0> = Client, <1> = Server Send */
    std::tuple<ChecksumCalculator, ChecksumCalculator> mRecv;

    /** @brief Raw strings for received data. <0> = Client, <1> = Server Recv */
    std::tuple<std::string, std::string> mRecvStr;

    /** @brief The mode of the AsyncSocket. */
    Mode mMode;

    std::mutex mChecksumAccess;
    std::mutex mStringAccess;
    OnCloseCallback mOnClose;
};

}  // namespace android::base::testing