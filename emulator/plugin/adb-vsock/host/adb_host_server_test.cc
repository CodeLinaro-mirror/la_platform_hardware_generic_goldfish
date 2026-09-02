// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/adb/adb_host_server.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

#include "android/base/testing/needs_winsock.h"
#include "android/base/testing/test_system.h"
#include "test_input_buffer_socket_server_thread.h"

namespace goldfish::adb {

using android::base::TestSystem;

TEST(AdbHostServer, notify) {
    // Bind to random port to listen to
    android::base::TestInputBufferSocketServerThread serverThread;
    ASSERT_TRUE(serverThread.valid());

    const int emulatorPort = 7648;  // Don't use default (5554) here.
    int clientPort = serverThread.port();
    serverThread.start();

    // Send a message to the server thread.
    EXPECT_TRUE(AdbHostServer::notify(emulatorPort, clientPort));

    intptr_t buffer_size = serverThread.wait();

    // Verify message content.
    constexpr std::string_view kExpected = "0012host:emulator:7648";
    EXPECT_EQ(kExpected.size(), static_cast<size_t>(buffer_size));
    EXPECT_EQ(kExpected.size(), serverThread.view().size());
    EXPECT_STREQ(kExpected.data(), serverThread.view().data());
}

TEST(AdbHostServer, getClientPortDefault) {
    TestSystem testSystem("/bin");
    const int expected = AdbHostServer::kDefaultAdbClientPort;
    EXPECT_EQ(expected, AdbHostServer::getClientPort());
}

TEST(AdbHostServer, getClientPortWithEnvironmentOverride) {
    TestSystem testSystem("/bin");
    testSystem.EnvSet("ANDROID_ADB_SERVER_PORT", "1234");
    EXPECT_EQ(1234, AdbHostServer::getClientPort());
}

TEST(AdbHostServer, getClientPortWithInvalidEnvironmentOverride) {
    TestSystem testSystem("/bin");
    testSystem.EnvSet("ANDROID_ADB_SERVER_PORT", "-1000");
    EXPECT_EQ(-1, AdbHostServer::getClientPort());

    testSystem.EnvSet("ANDROID_ADB_SERVER_PORT", "65536");
    EXPECT_EQ(-1, AdbHostServer::getClientPort());
}

namespace {

class MockAdbShellServerThread {
  public:
    MockAdbShellServerThread(bool transportOk = true, bool shellOk = true)
            : mSocket(android::base::socketTcp4LoopbackServer(0))
            , mTransportOk(transportOk)
            , mShellOk(shellOk) {}

    ~MockAdbShellServerThread() { wait(); }

    void start() {
        mThread = std::thread([this]() { run(); });
    }

    void wait() {
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    bool valid() const { return mSocket.valid(); }
    int port() const { return android::base::socketGetPort(mSocket.get()); }

    const std::string& transportReceived() const { return mTransportReceived; }
    const std::string& shellReceived() const { return mShellReceived; }

  private:
    void run() {
        int fd = android::base::socketAcceptAny(mSocket.get());
        if (fd < 0) return;

        mTransportReceived = readProtocolMessage(fd);
        if (!mTransportOk) {
            android::base::socketSendAll(fd, "FAIL", 4);
            android::base::socketClose(fd);
            return;
        }
        android::base::socketSendAll(fd, "OKAY", 4);

        mShellReceived = readProtocolMessage(fd);
        if (!mShellOk) {
            android::base::socketSendAll(fd, "FAIL", 4);
            android::base::socketClose(fd);
            return;
        }
        android::base::socketSendAll(fd, "OKAY", 4);

        android::base::socketClose(fd);
    }

    static std::string readProtocolMessage(int fd) {
        char lenBuf[4] = {0};
        size_t lenRead = 0;
        while (lenRead < 4) {
            ssize_t ret = android::base::socketRecv(fd, &lenBuf[lenRead], 4 - lenRead);
            if (ret <= 0) return "";
            lenRead += ret;
        }
        uint32_t len = 0;
        if (sscanf(std::string(lenBuf, 4).c_str(), "%04x", &len) != 1) return "";
        std::string payload(len, '\0');
        size_t bytesRead = 0;
        while (bytesRead < len) {
            ssize_t ret = android::base::socketRecv(fd, &payload[bytesRead], len - bytesRead);
            if (ret <= 0) break;
            bytesRead += ret;
        }
        payload.resize(bytesRead);
        return payload;
    }

    android::base::ScopedSocket mSocket;
    bool mTransportOk;
    bool mShellOk;
    std::string mTransportReceived;
    std::string mShellReceived;
    std::thread mThread;
};

}  // namespace

TEST(AdbHostServer, runShellCommand_successWithSerial) {
    MockAdbShellServerThread server(/*transportOk=*/true, /*shellOk=*/true);
    ASSERT_TRUE(server.valid());
    server.start();

    EXPECT_TRUE(
            AdbHostServer::runShellCommand("cmd alarm set-time 1788360000", 5554, server.port()));
    server.wait();

    EXPECT_EQ("host:transport:emulator-5554", server.transportReceived());
    EXPECT_EQ("shell:cmd alarm set-time 1788360000", server.shellReceived());
}

TEST(AdbHostServer, runShellCommand_successAnySerial) {
    MockAdbShellServerThread server(/*transportOk=*/true, /*shellOk=*/true);
    ASSERT_TRUE(server.valid());
    server.start();

    EXPECT_TRUE(AdbHostServer::runShellCommand("settings put secure test 1", 0, server.port()));
    server.wait();

    EXPECT_EQ("host:transport-any", server.transportReceived());
    EXPECT_EQ("shell:settings put secure test 1", server.shellReceived());
}

TEST(AdbHostServer, runShellCommand_transportFailure) {
    MockAdbShellServerThread server(/*transportOk=*/false, /*shellOk=*/true);
    ASSERT_TRUE(server.valid());
    server.start();

    EXPECT_FALSE(
            AdbHostServer::runShellCommand("cmd alarm set-time 1788360000", 5554, server.port()));
    server.wait();
}

TEST(AdbHostServer, runShellCommand_shellFailure) {
    MockAdbShellServerThread server(/*transportOk=*/true, /*shellOk=*/false);
    ASSERT_TRUE(server.valid());
    server.start();

    EXPECT_FALSE(
            AdbHostServer::runShellCommand("cmd alarm set-time 1788360000", 5554, server.port()));
    server.wait();
}

TEST(AdbHostServer, runShellCommand_connectionFailure) {
    EXPECT_FALSE(AdbHostServer::runShellCommand("cmd alarm set-time 1788360000", 5554, -1));
}

}  // namespace goldfish::adb
