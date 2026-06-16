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

#include <string_view>

#include "TestInputBufferSocketServerThread.h"
#include "android/base/testing/needs_winsock.h"
#include "android/base/testing/test_system.h"

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

}  // namespace goldfish::adb
