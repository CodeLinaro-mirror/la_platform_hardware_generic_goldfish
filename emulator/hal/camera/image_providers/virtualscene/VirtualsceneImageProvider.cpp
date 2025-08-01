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

#include "VirtualsceneImageProvider.h"

#include "absl/log/log.h"

namespace goldfish::camera_image_providers::virtualscene {

const char* VirtualsceneImageProvider::getId() const { return "virtualscene"; }

int VirtualsceneImageProvider::start(const CameraImageProviderStreamConfig* s, unsigned n) {
    VLOG(1) << getId() << ":  start {";
    for (; n > 0; --n, ++s) {
        VLOG(1) << "    { id=" << s->id << " format=" << s->format << " size=" << s->size.width
                << "x" << s->size.height << " }";
    }
    VLOG(1) << "}";
    return 0;
}

int VirtualsceneImageProvider::capture(
        const CameraImageProviderCaptureOpts& opts,
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

void VirtualsceneImageProvider::stop() { VLOG(1) << getId() << ":  stop"; }

void* VirtualsceneImageProvider::create(const CameraImageProviderInfo&) {
    return new VirtualsceneImageProvider();
}

}  // namespace goldfish::camera_image_providers::virtualscene