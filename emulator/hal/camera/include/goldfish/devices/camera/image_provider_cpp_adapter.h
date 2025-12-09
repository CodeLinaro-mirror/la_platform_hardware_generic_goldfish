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

#pragma once

#include "android/camera/camera_image_provider_api.h"

namespace goldfish::devices::camera {

template <typename IMPL>
struct ImageProviderCppAdapter {
    static void* create(const CameraImageProviderInfo* info,
                        const CameraImageProviderVtbl** ppVtbl) {
        static const CameraImageProviderVtbl vtbl = {
            .getId = [](const void* that) { return static_cast<const IMPL*>(that)->getId(); },
            .start = [](void* that, const CameraImageProviderStreamConfig* s,
                        unsigned n) { return static_cast<IMPL*>(that)->start(s, n); },
            .capture =
                    [](void* that, const CameraImageProviderCaptureOpts* opts,
                       CameraImageProviderStreamCaptureSink sink, void* sinkOpaque,
                       const CameraImageProviderStreamCaptureInfo* sci, unsigned scin) {
                        return static_cast<IMPL*>(that)->capture(*opts, sink, sinkOpaque, sci,
                                                                 scin);
                    },
            .stop = [](void* that) { static_cast<IMPL*>(that)->stop(); },
            .dctor = [](void* that) { delete static_cast<IMPL*>(that); },
        };

        void* instance = IMPL::create(*info);
        *ppVtbl = instance ? &vtbl : nullptr;

        return instance;
    }
};

}  // namespace goldfish::devices::camera
