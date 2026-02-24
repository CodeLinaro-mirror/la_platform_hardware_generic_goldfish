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
using ::goldfish::parsing::FromChars;

namespace {
struct RutabagaImpl : public IGrallocDetails {
    explicit RutabagaImpl(struct rutabaga* r) : rutabaga(r) {}

    ImageFormat GetImageFormat(const AndroidPixelFormat apf) const override {
        switch (apf) {
        case AndroidPixelFormat::kRgba8888:
            return ImageFormat::kRgba8888;

        case AndroidPixelFormat::kYcbcr420888:
            return ImageFormat::kYuV420NV12;

        case AndroidPixelFormat::kUnspecified:
            break;
        }

        return ImageFormat::kNone;
    }

    int Transfer(const std::string_view handle_str, const ImageRef& img) const override {
        const std::optional<uint32_t> maybe_resource_id = FromChars<uint32_t>(handle_str);
        if (!maybe_resource_id) {
            return -1;
        }

        const auto img_size = img.GetSize();
        const auto img_data = img.GetData();
        return rutabagaImageTransfer(rutabaga, maybe_resource_id.value(), img_size.width,
                                     img_size.height, GetStride(img), img_data.first,
                                     img_data.second);
    }

    struct rutabaga* rutabaga;
};
}  // namespace

GrallocDetailsPtr GetGrallocImpl() {
    struct rutabaga* r = rutabagaGetInstance();
    if (!r) {
        return nullptr;
    }

    return std::make_shared<RutabagaImpl>(r);
}

}  // namespace goldfish::avd_info
