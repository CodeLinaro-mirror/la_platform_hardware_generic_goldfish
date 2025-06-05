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

#include "android/camera/image_providers/webcam.h"

#include <cassert>
#include <cstring>
#include <string>

#include "absl/log/log.h"

namespace {

std::string buildId(const CameraImageProviderInfo& info) {
    using namespace std::literals;
    return "webcam:"s + static_cast<const char*>(info.createArg);
}

struct WebcamImageProvider {
    WebcamImageProvider(const CameraImageProviderInfo& info) : mId(buildId(info)) {}

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

    static void* create(const CameraImageProviderInfo* info,
                        const CameraImageProviderVtbl** ppVtbl) {
        using Impl = WebcamImageProvider;

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
            .dctor = [](void* that) { delete static_cast<Impl*>(that); },
        };

        *ppVtbl = &vtbl;
        return new Impl(*info);
    }

    const std::string mId;
};

void createArgDctor(void* arg) {}

}  // namespace

int enumerateWebcamImageProviders(void (*sink)(void*, CameraImageProviderInfo*), void* opaque) {
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
        .create = &WebcamImageProvider::create,
        .createArgDctor = &createArgDctor,
    };

    const CameraImageProviderInfo tmpl = {
        .vtbl = &vtbl,
        .supportedFrameSizes = supportedFrameSizes,
        .createArg = nullptr,
        .supportedFrameSizesNum = sizeof(supportedFrameSizes) / sizeof(supportedFrameSizes[0]),
        .isBackFacing = 0,
        .needFreeSupportedSizes = 0,
    };

    CameraImageProviderInfo toAdd;

    toAdd = tmpl;
    toAdd.createArg = "abc:42";
    (*sink)(opaque, &toAdd);

    toAdd = tmpl;
    toAdd.createArg = "1/2/3";
    (*sink)(opaque, &toAdd);

    return 0;
}
