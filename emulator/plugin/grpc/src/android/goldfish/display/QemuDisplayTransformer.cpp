// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "android/goldfish/display/QemuDisplayTransformer.h"

#include <qemu/typedefs.h>

#include <cmath>
#include <cstddef>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "host-common/opengles.h"
#include "render-utils/Renderer.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/surface.h"

// IWYU pragma: end_keep
// clang-format on
}
namespace android {
namespace goldfish {

using android::emulation::control::ImageTransport;

static pixman_format_code_t pixmanFormat(ImageFormat format) {
    switch (format.format()) {
        case ImageFormat::RGBA8888:
            return PIXMAN_a8r8g8b8;
        case ImageFormat::RGB888:
            return PIXMAN_b8g8r8;
        default:
            LOG(ERROR) << "Format: " << format.ShortDebugString() << ", not supported.";
            return PIXMAN_a8r8g8b8;
    }
}

static void copy_pixman_image_to_buffer(pixman_image_t* image, uint8_t* pixels, size_t cPixels) {
    int height = pixman_image_get_height(image);
    int stride = pixman_image_get_stride(image);  // Bytes per row
    uint8_t* source_data = (uint8_t*)pixman_image_get_data(image);

    // Calculate total size to copy (height * stride)
    size_t total_size = height * stride;

    total_size = std::min(total_size, cPixels);
    memcpy(pixels, source_data, total_size);
}

static absl::Status copy_buffer_to_pixman_image(pixman_image_t* src_img, const ImageFormat& format,
                                                Image& image) {
    // TODO(jansene): We need to calculate the actual rotation and
    // proper width and height based upon that, versus the raw scaled pixels.

    if (format.transport().channel() == ImageTransport::MMAP) {
        return absl::Status(absl::StatusCode::kUnimplemented, "MMap is not supported yet.");
    }

    // Next we are going to transform the image.
    pixman_image_t* dst_img;
    pixman_transform_t transform;

    // Create the destination image
    dst_img = pixman_image_create_bits(pixmanFormat(format), format.width(), format.height(), NULL,
                                       0);

    // Set up the transformation
    pixman_transform_init_scale(&transform,
                                (double)format.width() / pixman_image_get_width(src_img),
                                (double)format.height() / pixman_image_get_height(src_img));

    // Apply the transformation and compositeE
    pixman_image_set_transform(dst_img, &transform);
    pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, format.width(),
                           format.height());

    int height = pixman_image_get_height(dst_img);
    int stride = pixman_image_get_stride(dst_img);
    int width = pixman_image_get_width(dst_img);
    // Calculate total size to copy (height * stride)
    size_t cPixels = height * stride;
    image.mutable_format()->set_height(height);
    image.mutable_format()->set_width(width);

    // Make sure the image field has a string that is large enough.
    if (image.image().size() != cPixels) {
        VLOG(1) << "Allocation of string object. " << image.image().size() << " < " << cPixels;
        auto buffer = new std::string(cPixels, 0);
        // The protobuf message takes ownership of the pointer.
        image.set_allocated_image(buffer);
    }
    // The underlying char* is r/w since c++17.
    char* unsafe = image.mutable_image()->data();
    uint8_t* pixels = reinterpret_cast<uint8_t*>(unsafe);
    uint8_t* source_data = (uint8_t*)pixman_image_get_data(dst_img);

    memcpy(pixels, source_data, cPixels);

    // Clean up
    pixman_image_unref(dst_img);
    return absl::OkStatus();
}

Image QemuDisplayTransformer::getScreenshot(ImageFormat fmt) {
    const auto& renderer = android_getOpenglesRenderer();
    if (!renderer) {
        LOG(ERROR) << "Failed to get opengles renderer";
        return Image();
    }

    LOG(INFO) << "Getting screenshot";
    unsigned int nChannels = 4;
    unsigned int width;
    unsigned int height;
    if (fmt.format() == ImageFormat::RGB888) {
        nChannels = 3;
    }
    size_t cPixels = 0;
    Image image;

    if (renderer->getScreenshot(nChannels, &width, &height, nullptr, &cPixels, fmt.display(),
                                fmt.width(), fmt.height(), 0) != 0) {
        LOG(INFO) << "Need: " << cPixels << " bytes";
        if (image.image().size() != cPixels) {
            VLOG(1) << "Allocation of string object. " << image.image().size() << " < " << cPixels;
            auto buffer = new std::string(cPixels, 0);
            // The protobuf message takes ownership of the pointer.
            image.set_allocated_image(buffer);
        }
        // The underlying char* is r/w since c++17.
        char* unsafe = image.mutable_image()->data();
        uint8_t* pixels = reinterpret_cast<uint8_t*>(unsafe);
        renderer->getScreenshot(nChannels, &width, &height, pixels, &cPixels, fmt.display(),
                                fmt.width(), fmt.height(), 0);
    }

    image.mutable_format()->CopyFrom(fmt);
    LOG(INFO) << "QemuDisplayTransformer::getScreenshot: " << image.ShortDebugString();
    return image;
}

ImageEventSupport* QemuDisplayTransformer::addListener(ImageFormat fmt) {
    const std::lock_guard<std::mutex> lock(mListenerLock);
    LOG(INFO) << "QemuDisplayTransformer::addListener: " << fmt.ShortDebugString();
    if (!mListenerMap.count(fmt)) {
        Image img;
        img.mutable_format()->CopyFrom(fmt);

        auto event_support = std::make_unique<ImageEventSupport>();
        mListenerMap[fmt] = std::make_tuple<std::unique_ptr<ImageEventSupport>, Image>(
                std::move(event_support), std::move(img));
    }
    auto it = mListenerMap.find(fmt);
    if (it != mListenerMap.end()) {
        return std::get<0>(it->second).get();
    }
    LOG(ERROR) << "Failed to find listener for format: " << fmt.ShortDebugString();
    return nullptr;
}

void QemuDisplayTransformer::fireEvent(pixman_image_t* src_img) {
    const std::lock_guard<std::mutex> lock(mListenerLock);
    std::vector<ImageFormat> expired;
    // Step 1, transform.
    for (auto& [format, image_pair] : mListenerMap) {
        auto& [listener, image] = image_pair;
        if (listener->size() == 0) {
            expired.push_back(format);
            continue;
        }

        auto status = copy_buffer_to_pixman_image(src_img, format, image);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to copy buffer to pixman image, skipping. " << status.message();
            continue;
        }

        LOG(INFO) << "QemuDisplayTransformer::fireEvent: " << format.ShortDebugString();
        listener->fireEvent(image);
    }
    // Step 2, send out events.
    for (const auto& invalid : expired) {
        mListenerMap.erase(invalid);
    }
}

}  // namespace goldfish
}  // namespace android