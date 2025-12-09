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

#include "goldfish/avd_info/gralloc_impl.h"

#include "goldfish/imaging/query_info.h"
#include "goldfish/parsing/from_chars.h"

extern "C" {
#include "goldfish/avd/rutabaga_glue.h"
}  // extern "C"

namespace goldfish::avd_info {
using ::goldfish::devices::camera::GrallocDetailsPtr;
using ::goldfish::devices::camera::IGrallocDetails;
using ::goldfish::imaging::AndroidPixelFormat;
using ::goldfish::imaging::ImageFormat;
using ::goldfish::imaging::ImageRef;
using ::goldfish::parsing::fromChars;

namespace {
struct RutabagaImpl : public IGrallocDetails {
    explicit RutabagaImpl(struct rutabaga* r) : mRutabaga(r) {}

    ImageFormat getImageFormat(const AndroidPixelFormat apf) const override {
        switch (apf) {
        case AndroidPixelFormat::RGBA_8888:
            return ImageFormat::RGBA_8888;

        case AndroidPixelFormat::YCBCR_420_888:
            return ImageFormat::YUV420_NV12;

        case AndroidPixelFormat::UNSPECIFIED:
            break;
        }

        return ImageFormat::NONE;
    }

    int transfer(const std::string_view handleStr, const ImageRef& img) const override {
        const std::optional<uint32_t> maybeResourceId = fromChars<uint32_t>(handleStr);
        if (!maybeResourceId) {
            return -1;
        }

        const auto imgSize = img.getSize();
        const auto imgData = img.getData();
        return rutabagaImageTransfer(mRutabaga, maybeResourceId.value(), imgSize.width,
                                     imgSize.height, getStride(img), imgData.first, imgData.second);
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

}  // namespace goldfish::avd_info
