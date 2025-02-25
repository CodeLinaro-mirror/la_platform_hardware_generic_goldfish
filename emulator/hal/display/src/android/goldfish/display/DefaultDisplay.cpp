// Copyright 2025 The Android Open Source Project
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
#include "android/goldfish/display/DefaultDisplay.h"

#include <cstddef>
#include <cstdint>

#include "absl/log/log.h"

#include "android/goldfish/display/NullDisplay.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace {

static pixman_format_code_t pixmanFormat(const ::android::goldfish::PixelFormat& format) {
    switch (format) {
        case ::android::goldfish::PixelFormat::RGBA8888:
            return PIXMAN_a8r8g8b8;
        case ::android::goldfish::PixelFormat::RGB888:
            return PIXMAN_b8g8r8;
        default:
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
}  // namespace

namespace android::goldfish {

static ::InputMultiTouchType translate_touch_type(MultiTouchType type) {
    switch (type) {
        case MultiTouchType::BEGIN:
            return INPUT_MULTI_TOUCH_TYPE_BEGIN;
        case MultiTouchType::UPDATE:
            return INPUT_MULTI_TOUCH_TYPE_UPDATE;
        case MultiTouchType::END:
            return INPUT_MULTI_TOUCH_TYPE_END;
        case MultiTouchType::CANCEL:
            return INPUT_MULTI_TOUCH_TYPE_CANCEL;
        case MultiTouchType::DATA:
            return INPUT_MULTI_TOUCH_TYPE_DATA;
    }
}

SharedDisplay IDisplay::nullDisplay() {
    static auto nullDisplay = std::make_shared<NullDisplay>();
    return nullDisplay;
}

DefaultDisplay::DefaultDisplay(QemuConsole* console, DisplaySurface* ds, int id)
    : IDisplay(id, 0, 0), mConsole(console) {
    if (!mConsole) {
        LOG(INFO) << "Display: " << id << " is using the default <null> console";
    }
    replaceSurface(ds);
}

void DefaultDisplay::replaceSurface(DisplaySurface* newSurface) {
    absl::MutexLock lock(&mDisplayAccess);
    mDisplaySurface = newSurface;
    mWidth = pixman_image_get_width(mDisplaySurface->image);
    mHeight = pixman_image_get_height(mDisplaySurface->image);
}

absl::StatusOr<FrameInfo> DefaultDisplay::getPixels(PixelFormat format, int newWidth, int newHeight,
                                                    int rotation, uint8_t* pixels,
                                                    size_t* cPixels) const {
    auto pixman_fmt = pixmanFormat(format);
    auto bpp = PIXMAN_FORMAT_BPP(pixman_fmt);
    auto stride = ((newWidth * bpp + 0x1f) >> 5) * sizeof(uint32_t);

    size_t requiredSize = newHeight * stride;

    if (requiredSize > *cPixels) {
        auto old = *cPixels;
        *cPixels = requiredSize;
        return absl::FailedPreconditionError(
                absl::StrFormat("Buffer too small; need %u bytes, have %u", requiredSize, old));
    }

    absl::MutexLock lock(&mDisplayAccess);
    ::pixman_image_t* dst_img;
    ::pixman_image_t* src_img = mDisplaySurface->image;
    ::pixman_transform_t transform;

    if (!src_img) {
        return absl::UnavailableError("No frame has been produced yet.");
    }

    uint32_t* pixel = (uint32_t*)pixels;
    // Create the destination image
    dst_img = pixman_image_create_bits(pixman_fmt, newWidth, newHeight, pixel, stride);

    assert(pixman_image_get_width(src_img) == mWidth);
    assert(pixman_image_get_height(src_img) == mHeight);

    // Set up the transformation (scale and rotate)
    pixman_transform_init_identity(&transform);  // Start with identity

    // Apply scaling
    pixman_transform_scale(&transform, NULL, pixman_double_to_fixed((double)newWidth / mWidth),
                           pixman_double_to_fixed((double)newHeight / mHeight));

    // Apply rotation around the center
    double angleRadians = rotation * M_PI / 180.0;
    pixman_fixed_t cos_val = pixman_double_to_fixed(cos(angleRadians));
    pixman_fixed_t sin_val = pixman_double_to_fixed(sin(angleRadians));

    pixman_transform_translate(&transform, NULL, pixman_int_to_fixed(newWidth / 2),
                               pixman_int_to_fixed(newHeight / 2));
    pixman_transform_rotate(&transform, NULL, cos_val, sin_val);
    pixman_transform_translate(&transform, NULL, pixman_int_to_fixed(-newWidth / 2),
                               pixman_int_to_fixed(-newHeight / 2));
    pixman_image_set_transform(dst_img, &transform);

    pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, mWidth,
                           mHeight);

    int dheight = pixman_image_get_height(dst_img);
    int dstride = pixman_image_get_stride(dst_img);

    assert(dstride != 0);

    // Calculate total size to copy (height * stride)
    *cPixels = dheight * dstride;
    uint8_t* source_data = (uint8_t*)pixman_image_get_data(dst_img);

    memcpy(pixels, source_data, *cPixels);

    // Clean up
    pixman_image_unref(dst_img);
    return mSeq;
}

void DefaultDisplay::sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    Error* error_warn;
    auto ttype = translate_touch_type(type);
    console_handle_touch_event(mConsole, mTouchSlots, slot, mWidth, mHeight, x, y, ttype,
                               &error_warn);
    warn_report_err(error_warn);
}

void DefaultDisplay::sendMouseEvent(int x, int y, int button_mask) {
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
            [INPUT_BUTTON_LEFT] = 0x01,       [INPUT_BUTTON_MIDDLE] = 0x02,
            [INPUT_BUTTON_RIGHT] = 0x04,      [INPUT_BUTTON_WHEEL_UP] = 0x08,
            [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
    };

    if (mlast_bmask != button_mask) {
        qemu_input_update_buttons(mConsole, bmap, mlast_bmask, button_mask);
        mlast_bmask = button_mask;
    }

    qemu_input_queue_abs(mConsole, INPUT_AXIS_X, x, 0, mWidth);
    qemu_input_queue_abs(mConsole, INPUT_AXIS_Y, y, 0, mHeight);
    qemu_input_event_sync();
}

void DefaultDisplay::updateSurface(int x, int y, int width, int height) {
    frameReceived();
}
}  // namespace android::goldfish