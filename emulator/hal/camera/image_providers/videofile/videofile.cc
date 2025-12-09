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

#include "android/camera/image_providers/videofile.h"

#include <cassert>
#include <cstring>
#include <string>

#include "absl/log/log.h"

#include "goldfish/devices/camera/image_provider_cpp_adapter.h"

using goldfish::devices::camera::ImageProviderCppAdapter;

namespace {

std::string buildId(const CameraImageProviderInfo& info) {
    using namespace std::literals;
    return "videofile:"s + static_cast<const char*>(info.createArg);
}

struct VideofileImageProvider {
    VideofileImageProvider(const CameraImageProviderInfo& info) : mId(buildId(info)) {}

    const char* getId() const { return mId.c_str(); }

    int start(const CameraImageProviderStreamConfig* s, unsigned n) {
        VLOG(1) << getId() << ":  start {";
        for (; n > 0; --n, ++s) {
            VLOG(1) << "    { id=" << s->id << " format=" << s->format << " size=" << s->size.width
                    << "x" << s->size.height << " }";
        }
        VLOG(1) << "}";
        return 0;
    }

    int capture(const CameraImageProviderCaptureOpts& opts,
                const CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
                const CameraImageProviderStreamCaptureInfo* sci, unsigned scin) {
        VLOG(1) << getId() << ":  capture {";
        for (; scin > 0; --scin, ++sci) {
            const CameraImageProviderStreamConfig* s = sci->cfg;
            VLOG(1) << "    { id=" << s->id << " format=" << s->format << " size=" << s->size.width
                    << "x" << s->size.height << " }";
        }
        VLOG(1) << "}";

        return 0;
    }

    void stop() { VLOG(1) << getId() << ":  stop"; }

    static void* create(const CameraImageProviderInfo& info) {
        return new VideofileImageProvider(info);
    }

    const std::string mId;
};

void createArgDctor(void* arg) {
    ::free(arg);
}

}  // namespace

int getVideofileImageProviderInfo(CameraImageProviderInfo* dst, const unsigned isBackFacing,
                                  const char* params) {
    static const CameraImageProviderRect supportedFrameSizes[] = {
        {
            .width = 640,
            .height = 480,
        },
        {
            .width = 352,
            .height = 288,
        },
        {
            .width = 320,
            .height = 240,
        },
        {
            .width = 176,
            .height = 144,
        },
        {
            .width = 1280,
            .height = 720,
        },
        {
            .width = 1280,
            .height = 960,
        },
    };

    static const CameraImageProviderInfoVtbl vtbl = {
        .create = &ImageProviderCppAdapter<VideofileImageProvider>::create,
        .createArgDctor = &createArgDctor,
    };

    *dst = (CameraImageProviderInfo){
        .vtbl = &vtbl,
        .supportedFrameSizes = supportedFrameSizes,
        .createArg = ::strdup(params),
        .supportedFrameSizesNum = sizeof(supportedFrameSizes) / sizeof(supportedFrameSizes[0]),
        .isBackFacing = static_cast<uint8_t>(isBackFacing),
        .needFreeSupportedSizes = 0,
    };

    return 0;
}
