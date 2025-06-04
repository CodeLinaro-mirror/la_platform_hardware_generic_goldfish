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

#include "android/goldfish/GrallocImpl.h"

#include "goldfish/parsing/fromChars.h"

extern "C" {
#include "android/goldfish/rutabaga_glue.h"
}  // extern "C"

namespace android::goldfish::avd_info {
using ::goldfish::devices::camera::GrallocDetailsPtr;
using ::goldfish::devices::camera::IGrallocDetails;
using ::goldfish::parsing::fromChars;

namespace {
struct RutabagaImpl : public IGrallocDetails {
    explicit RutabagaImpl(struct rutabaga* r) : mRutabaga(r) {}

    uint32_t aFormatToFourCC(const uint32_t androidFormat) const override { return androidFormat; }

    int transfer(const std::string_view handleStr, const uint32_t format, const uint32_t width,
                 const uint32_t height, const void* const framebuffer,
                 const size_t framebufferSize) const override {
        const std::optional<uint32_t> maybeResourceId = fromChars<uint32_t>(handleStr);
        if (!maybeResourceId) {
            return -1;
        }

        return rutabagaImageTransfer(mRutabaga, maybeResourceId.value(), width, height,
                                     getStride(format, width), framebuffer, framebufferSize);
    }

    uint32_t getStride(const uint32_t format, const uint32_t width) const {
        return (format == 1) ? (width * 4U) : 0U;
    }

    struct rutabaga* mRutabaga;
};
}  // namespace

GrallocDetailsPtr getGrallocImpl() {
    struct rutabaga* r = rutabagaGetInstance();
    if (!r) {
        return nullptr;
    }

    return std::make_shared<RutabagaImpl>(r);
}

}  // namespace android::goldfish::avd_info
