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

#include "android/camera/CameraDevice.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "goldfish/devices/cable/cable.h"

using namespace std::literals;
using namespace goldfish::devices::camera;

using goldfish::devices::cable::ISocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;
using goldfish::imaging::AndroidPixelFormat;
using goldfish::imaging::ImageFormat;
using goldfish::imaging::ImageRef;

using ::testing::ElementsAreArray;

bool operator==(const CameraImageProviderStreamConfig& lhs,
                const CameraImageProviderStreamConfig& rhs) {
    return (lhs.id == rhs.id) && (lhs.format == rhs.format) && (lhs.size.width == rhs.size.width) &&
           (lhs.size.height == rhs.size.height);
}

namespace {
struct TestGralloc : public IGrallocDetails {
    struct ImageTransfer {
        std::string handle;
        size_t framebufferSize;
        ImageFormat format;
        uint32_t width;
        uint32_t height;

        bool operator==(const ImageTransfer& rhs) const {
            return (handle == rhs.handle) && (framebufferSize == rhs.framebufferSize) &&
                   (format == rhs.format) && (width == rhs.width) && (height == rhs.height);
        }
    };

    ImageFormat getImageFormat(const AndroidPixelFormat apf) const override {
        switch (apf) {
        case AndroidPixelFormat::RGBA_8888:
            return ImageFormat::RGBA_8888;

        case AndroidPixelFormat::YCBCR_420_888:
            return ImageFormat::YUV420_NV12;

        default:
            return ImageFormat::NONE;
        }
    }

    int transfer(const std::string_view handleStr, const ImageRef& img) const override {
        ImageTransfer transfer = {
            .handle = std::string(handleStr),
            .framebufferSize = img.getData().second,
            .format = img.getFormat(),
            .width = img.getSize().width,
            .height = img.getSize().height,
        };

        mTransfers.push_back(std::move(transfer));
        return 0;
    }

    mutable std::vector<ImageTransfer> mTransfers;
};

struct TestSocket : public ISocket {
    void sendAsync(const void* str, size_t size) override {
        data.append(static_cast<const char*>(str), size);
    }

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr unplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->onReceive(data.data(), data.size()); }

    PlugPtr plug;
    std::string data;
};

struct CameraDeviceTest : public ::testing::Test {
    const char* getId() const { return "id"; }

    int start(const CameraImageProviderStreamConfig* s, unsigned n) {
        mStreamConfigs.assign(s, s + n);
        return 0;
    }

    int capture(const CameraImageProviderCaptureOpts& opts,
                CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
                const CameraImageProviderStreamCaptureInfo* sci, unsigned scin) {
        mCaptureOpts = opts;

        for (; scin; ++sci, --scin) {
            sink(sinkOpaque, sci, nullptr, scin);
        }

        return 0;
    }

    void stop() { mStopCalled = true; }

    void dctor() {}

    void SetUp() override {
        using Impl = CameraDeviceTest;

        static const CameraImageProviderVtbl vtbl = {
            .getId = [](const void* that) { return static_cast<const Impl*>(that)->getId(); },
            .start = [](void* that, const CameraImageProviderStreamConfig* s,
                        unsigned n) { return static_cast<Impl*>(that)->start(s, n); },
            .capture =
                    [](void* that, const CameraImageProviderCaptureOpts* opts,
                       CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
                       const CameraImageProviderStreamCaptureInfo* sci, unsigned scin) {
                        return static_cast<Impl*>(that)->capture(*opts, sink, sinkOpaque, sci,
                                                                 scin);
                    },
            .stop = [](void* that) { static_cast<Impl*>(that)->stop(); },
            .dctor = [](void* that) { static_cast<Impl*>(that)->dctor(); },
        };

        mTestGrallocPtr = std::make_shared<TestGralloc>();
        mCameraDevice = std::make_shared<CameraDevice>(SocketPtr(&mTestSocket), this, &vtbl,
                                                       mTestGrallocPtr);
        mTestSocket.plug = mCameraDevice;
    }

    void TearDown() override { mTestSocket.plug->onUnplug(); }

    std::shared_ptr<TestGralloc> mTestGrallocPtr;
    TestSocket mTestSocket;
    std::shared_ptr<CameraDevice> mCameraDevice;
    std::vector<CameraImageProviderStreamConfig> mStreamConfigs;
    CameraImageProviderCaptureOpts mCaptureOpts;
    bool mStopCalled = false;
};

}  // namespace

TEST_F(CameraDeviceTest, configure) {
    static const char configureQuery[] = "configure streams=42:640x480@1,3:320x240@23";

    static const CameraImageProviderStreamConfig kExpectedConfigs[] = {
        {
            .id = 42,
            .format = GOLDFISH_IMAGE_FORMAT_RGBA_8888,
            .size =
                    {
                        .width = 640,
                        .height = 480,
                    },
        },
        {
            .id = 3,
            .format = GOLDFISH_IMAGE_FORMAT_YUV420_NV12,
            .size =
                    {
                        .width = 320,
                        .height = 240,
                    },
        },
    };

    EXPECT_TRUE(mTestSocket.plug->onReceive(configureQuery, sizeof(configureQuery)));
    EXPECT_FALSE(mStopCalled);
    EXPECT_THAT(mStreamConfigs, ElementsAreArray(kExpectedConfigs));

    EXPECT_TRUE(mTestSocket.plug->onReceive(configureQuery, sizeof(configureQuery)));
    EXPECT_TRUE(mStopCalled);
    EXPECT_THAT(mStreamConfigs, ElementsAreArray(kExpectedConfigs));
}

TEST_F(CameraDeviceTest, capture) {
    static const char configureQuery[] = "configure streams=0:640x480@1,1:320x240@23";
    static const char captureQuery[] = "capture bufs=0:abc,1:xyz";

    EXPECT_TRUE(mTestSocket.plug->onReceive(configureQuery, sizeof(configureQuery)));
    EXPECT_TRUE(mTestSocket.plug->onReceive(captureQuery, sizeof(captureQuery)));

    static const TestGralloc::ImageTransfer kTransfers[] = {
        {
            .handle = "abc"s,
            .framebufferSize = 2,
            .format = ImageFormat::RGBA_8888,
            .width = 640,
            .height = 480,
        },
        {
            .handle = "xyz"s,
            .framebufferSize = 1,
            .format = ImageFormat::YUV420_NV12,
            .width = 320,
            .height = 240,
        },
    };

    EXPECT_THAT(mTestGrallocPtr->mTransfers, ElementsAreArray(kTransfers));
}
