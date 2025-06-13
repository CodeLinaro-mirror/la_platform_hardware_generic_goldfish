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

#include "goldfish/avd/rutabaga_glue.h"

// clang-format off
// IWYU pragma: begin_keep
#include <qemu/osdep.h>
#include <qom/object.h>
#include <hw/virtio/virtio-gpu.h>
// IWYU pragma: end_keep
// clang-format on

#include <rutabaga_gfx/rutabaga_gfx_ffi_goldfish.h>

struct rutabaga* rutabagaGetInstance() {
    Object* obj = object_resolve_path_type("", TYPE_VIRTIO_GPU_RUTABAGA, NULL);
    if (!obj) {
        return NULL;
    }

    VirtIOGPURutabaga* vr = VIRTIO_GPU_RUTABAGA(obj);
    if (!vr) {
        return NULL;
    }

    return vr->rutabaga;
}

int32_t rutabagaImageTransfer(struct rutabaga* instance, const uint32_t resourceId,
                              const uint32_t width, const uint32_t height, const uint32_t stride,
                              const void* const framebuffer, const uint32_t framebufferSize) {
    struct rutabaga_transfer transfer = {
        .x = 0,
        .y = 0,
        .z = 0,
        .w = width,
        .h = height,
        .d = 1,
        .stride = stride,
        .layer_stride = 0,
        .offset = 0,
    };

    struct iovec iov = {
        .iov_base = (uint8_t*)framebuffer,
        .iov_len = framebufferSize,
    };

    return rutabaga_resource_transfer_write_goldfish(instance, 0, resourceId, &transfer, &iov);
}
