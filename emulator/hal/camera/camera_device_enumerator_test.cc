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

#include "goldfish/devices/camera/camera_device_enumerator.h"

#include <gmock/gmock.h>

#include <string>

#include "goldfish/devices/cable/cable.h"

using namespace std::literals;
using namespace goldfish::devices::camera;

using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::ISocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

namespace {
struct TestSocket : public ISocket {
    void SendAsync(const void* str, size_t size) override {
        data.append(static_cast<const char*>(str), size);
    }

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->OnReceive(data.data(), data.size()); }

    PlugPtr plug;
    std::string data;
};

const CameraImageProviderInfoVtbl kCameraImageProviderInfoVtbl = {};
}  // namespace

TEST(CameraDeviceEnumerator_cameraInfoToString, empty) {
    static const CameraImageProviderInfo info = {};
    EXPECT_EQ(CameraDeviceEnumerator::cameraInfoToString(42, info), ""s);
}

TEST(CameraDeviceEnumerator_cameraInfoToString, good) {
    static const CameraImageProviderRect sizes[] = {
        {
            .width = 640,
            .height = 480,
        },
        {
            .width = 300,
            .height = 200,
        },
    };

    CameraImageProviderInfo info = {
        .vtbl = nullptr,
        .supportedFrameSizes = sizes,
        .createArg = nullptr,
        .supportedFrameSizesNum = 2,
        .isBackFacing = 1,
        .needFreeSupportedSizes = 0,
    };

    EXPECT_EQ(CameraDeviceEnumerator::cameraInfoToString(42, info),
              "name=42 dir=back framedims=640x480,300x200\n"s);

    info.supportedFrameSizesNum = 1;
    info.isBackFacing = 0;

    EXPECT_EQ(CameraDeviceEnumerator::cameraInfoToString(18, info),
              "name=18 dir=front framedims=640x480\n"s);
}

TEST(CameraDeviceEnumerator, list) {
    CameraImageProviderRegistryPtr registry = std::make_shared<CameraImageProviderRegistry>();

    {
        static const CameraImageProviderRect sizes0[] = {
            {
                .width = 11,
                .height = 22,
            },
            {
                .width = 33,
                .height = 44,
            },
        };

        CameraImageProviderInfo info0 = {
            .vtbl = &kCameraImageProviderInfoVtbl,
            .supportedFrameSizes = sizes0,
            .createArg = nullptr,
            .supportedFrameSizesNum = 2,
            .isBackFacing = 1,
            .needFreeSupportedSizes = 0,
        };
        registry->add(info0);

        static const CameraImageProviderRect sizes1[] = {
            {
                .width = 55,
                .height = 66,
            },
        };

        CameraImageProviderInfo info1 = {
            .vtbl = &kCameraImageProviderInfoVtbl,
            .supportedFrameSizes = sizes1,
            .createArg = nullptr,
            .supportedFrameSizesNum = 1,
            .isBackFacing = 0,
            .needFreeSupportedSizes = 0,
        };
        registry->add(info1);
    }

    TestSocket testSocket;
    auto plug = std::make_shared<CameraDeviceEnumerator>(SocketPtr(&testSocket), registry);
    testSocket.plug = plug;

    static const char listQuery[] = "list";
    EXPECT_TRUE(testSocket.plug->OnReceive(listQuery, sizeof(listQuery)));

    EXPECT_EQ(testSocket.data,
              "0000004aok:"
              "name=0 dir=back framedims=11x22,33x44\n"
              "name=1 dir=front framedims=55x66\n"s);

    testSocket.plug->OnUnplug();
}

TEST(CameraDeviceEnumerator, empty_list) {
    CameraImageProviderRegistryPtr registry = std::make_shared<CameraImageProviderRegistry>();

    TestSocket testSocket;
    auto plug = std::make_shared<CameraDeviceEnumerator>(SocketPtr(&testSocket), registry);
    testSocket.plug = plug;

    static const char listQuery[] = "list";
    EXPECT_TRUE(testSocket.plug->OnReceive(listQuery, sizeof(listQuery)));

    EXPECT_EQ(testSocket.data, "00000004ok:\n"s);

    testSocket.plug->OnUnplug();
}
