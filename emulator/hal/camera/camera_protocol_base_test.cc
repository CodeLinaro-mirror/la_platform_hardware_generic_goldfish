/* Copyright 2025 The Android Open Source Project
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

#include "goldfish/devices/camera/camera_protocol_base.h"

#include <gmock/gmock.h>

#include "goldfish/devices/cable/cable.h"

using namespace std::literals;
using namespace goldfish::devices::camera;

using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::ISocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

namespace {
struct TestSocket : public ISocket {
    void sendAsync(const void* data, size_t size) override {}

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr unplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->onReceive(data.data(), data.size()); }

    PlugPtr plug;
};

struct CameraProtocolTest : public CameraProtocolBase {
    CameraProtocolTest(SocketPtr socket) : CameraProtocolBase(std::move(socket)) {}

    bool processQuery(std::string_view query, std::string_view params) override {
        mQuery = std::string(query);
        mParams = std::string(params);
        return true;
    }

    std::string mQuery;
    std::string mParams;
};
}  // namespace

TEST(CameraProtocolBase, processQuery) {
    TestSocket testSocket;
    auto plug = std::make_shared<CameraProtocolTest>(SocketPtr(&testSocket));
    testSocket.plug = plug;

    static const char catVideos[] = "cat videos";
    EXPECT_TRUE(testSocket.plug->onReceive(catVideos, sizeof(catVideos)));
    EXPECT_EQ(plug->mQuery, "cat"s);
    EXPECT_EQ(plug->mParams, "videos"s);

    testSocket.plug->onUnplug();
}
