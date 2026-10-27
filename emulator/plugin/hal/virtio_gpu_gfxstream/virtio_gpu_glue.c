/* Copyright 2026 The Android Open Source Project
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

#include "goldfish/devices/virtio_gpu_gfxstream/virtio_gpu_glue.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/virtio/virtio-gpu.h"
// IWYU pragma: end_keep
// clang-format on

/*
 * rutabaga_* entry points come from @qemu//google/rutabaga:rutabaga_ffi_lazy,
 * trampolines that dlopen() librutabaga_ffi on first use; nothing is loaded
 * unless a virtio-gpu-rutabaga device is actually in use.
 */
#include "gfxstream_renderer.h"
#include "rutabaga_gfx/rutabaga_gfx_ffi.h"
#include "rutabaga_gfx/rutabaga_gfx_ffi_goldfish.h"
#include "virtio_gpu_gfxstream_internal.h"

/*
 * Exactly one of the two device pointers is set. The renderer handle itself
 * (vgg->gfxstream / vr->rutabaga) is read on every call, not cached: the
 * device re-creates it on reset / failed restore and callers keep this
 * instance for the lifetime of the process.
 */
struct VirtioGpuGlue {
    VirtIOGPUGfxstream* gfxstream_dev;
    VirtIOGPURutabaga* rutabaga_dev;
};

static struct VirtioGpuGlue s_gfxstream_glue;
static struct VirtioGpuGlue s_rutabaga_glue;

static struct gfxstream* GlueGfxstream(const struct VirtioGpuGlue* instance) {
    return instance->gfxstream_dev ? instance->gfxstream_dev->gfxstream : NULL;
}

static struct rutabaga* GlueRutabaga(const struct VirtioGpuGlue* instance) {
    return instance->rutabaga_dev ? instance->rutabaga_dev->rutabaga : NULL;
}

struct VirtioGpuGlue* virtioGpuGlueGetInstance(void) {
    Object* obj = object_resolve_path_type("", TYPE_VIRTIO_GPU_GFXSTREAM, NULL);
    if (obj) {
        VirtIOGPUGfxstream* vgg = VIRTIO_GPU_GFXSTREAM(obj);
        if (!vgg || !vgg->gfxstream) {
            return NULL;
        }
        s_gfxstream_glue.gfxstream_dev = vgg;
        return &s_gfxstream_glue;
    }

    obj = object_resolve_path_type("", TYPE_VIRTIO_GPU_RUTABAGA, NULL);
    if (obj) {
        VirtIOGPURutabaga* vr = VIRTIO_GPU_RUTABAGA(obj);
        if (!vr || !vr->rutabaga) {
            return NULL;
        }
        s_rutabaga_glue.rutabaga_dev = vr;
        return &s_rutabaga_glue;
    }
    return NULL;
}

const char* virtioGpuGlueDeviceName(const struct VirtioGpuGlue* instance) {
    if (!instance) {
        return "none";
    }
    return instance->gfxstream_dev ? "virtio-gpu-gfxstream" : "virtio-gpu-rutabaga";
}

int32_t virtioGpuGlueImageTransfer(struct VirtioGpuGlue* instance, const uint32_t resource_id,
                                   const uint32_t width, const uint32_t height,
                                   const uint32_t stride, const void* const framebuffer,
                                   const uint32_t framebuffer_size) {
    if (!instance) {
        return -1;
    }

    struct iovec iov = {
        .iov_base = (void*)framebuffer,
        .iov_len = framebuffer_size,
    };

    if (instance->gfxstream_dev) {
        struct gfxstream* gfxstream = GlueGfxstream(instance);
        if (!gfxstream) {
            return -1;
        }
        struct gfxstream_transfer transfer = {
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
        return gfxstream_resource_transfer_write_goldfish(gfxstream, 0, resource_id, &transfer,
                                                          &iov);
    }

    struct rutabaga* rutabaga = GlueRutabaga(instance);
    if (!rutabaga) {
        return -1;
    }

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
    return rutabaga_resource_transfer_write_goldfish(rutabaga, 0, resource_id, &transfer, &iov);
}

int32_t virtioGpuGlueImageRead(struct VirtioGpuGlue* instance, uint32_t resource_id, uint32_t width,
                               uint32_t height, uint32_t stride, void* framebuffer,
                               uint32_t framebuffer_size) {
    if (!instance) {
        return -1;
    }

    struct iovec iov = {
        .iov_base = framebuffer,
        .iov_len = framebuffer_size,
    };

    if (instance->gfxstream_dev) {
        struct gfxstream* gfxstream = GlueGfxstream(instance);
        if (!gfxstream) {
            return -1;
        }
        struct gfxstream_transfer transfer = {
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
        return gfxstream_resource_transfer_read(gfxstream, 0, resource_id, &transfer, &iov);
    }

    struct rutabaga* rutabaga = GlueRutabaga(instance);
    if (!rutabaga) {
        return -1;
    }

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
    return rutabaga_resource_transfer_read(rutabaga, 0, resource_id, &transfer, &iov);
}
