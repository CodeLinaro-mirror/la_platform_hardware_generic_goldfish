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

#include "android/camera/VirtualsceneImageProvider.h"

#include <cassert>

#include "absl/log/log.h"

namespace {

struct VirtualsceneImageProvider {
    VirtualsceneImageProvider(const CameraImageProviderInfo& info) {}

    const char* getId() const { return "virtualscene"; }

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
        for (; scin > 0; --scin, ++sci) {
            const CameraImageProviderStreamConfig& cfg = *sci->cfg;
            if (cfg.format == 1) {
                const size_t width = cfg.size.width;
                const size_t height = cfg.size.height;
                std::vector<uint32_t> bitmap(width * height);

                uint32_t* p = bitmap.data();
                /*
                 * Produce a checkerboadr pattern of FF00FF00U and FF600060
                 * RGBA colors (the highest FF bits are the A component) with
                 * quares of (1 << 7) pixels: ((x >> 7) & 1) ^ ((y >> 7) & 1).
                 */
                for (size_t y = 0; y < height; ++y) {
                    for (size_t x = 0; x < width; ++x, ++p) {
                        *p = (((x ^ y) >> 7) & 1) ? 0xFF00FF00U : 0xFF600060U;
                    }
                }

                sink(sinkOpaque, sci, bitmap.data(), bitmap.size() * sizeof(uint32_t));
            }
        }

        return 0;
    }

    void stop() { VLOG(1) << getId() << ":  stop"; }

    static void* create(const CameraImageProviderInfo* info,
                        const CameraImageProviderVtbl** ppVtbl) {
        using Impl = VirtualsceneImageProvider;

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
};

}  // namespace

void createArgDctor(void* arg) {
    assert(!arg);
}

int getVirtualsceneImageProviderInfo(CameraImageProviderInfo* dst, const unsigned isBackFacing) {
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
        .create = &VirtualsceneImageProvider::create,
        .createArgDctor = &createArgDctor,
    };

    *dst = (CameraImageProviderInfo){
        .vtbl = &vtbl,
        .supportedFrameSizes = supportedFrameSizes,
        .createArg = nullptr,
        .supportedFrameSizesNum = sizeof(supportedFrameSizes) / sizeof(supportedFrameSizes[0]),
        .isBackFacing = static_cast<uint8_t>(isBackFacing),
        .needFreeSupportedSizes = 0,
    };

    return 0;
}
