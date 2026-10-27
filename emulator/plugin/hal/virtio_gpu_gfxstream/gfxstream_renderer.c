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

#include "gfxstream_renderer.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#ifndef _WIN32
#include <sys/uio.h> /* on Windows struct iovec comes from the gfxstream header */
#endif

#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "gfxstream_backend_loader.h"

/* Sub-directory of the snapshot's renderer/ directory, as rutabaga laid it out. */
#define GFXSTREAM_SNAPSHOT_SUBDIR "gfxstream"
#define GFXSTREAM_DEFAULT_CONTEXT_NAME "gpu_renderer"

struct gfxstream {
    const struct gfxstream_backend_fns* fns;
    struct gfxstream_builder builder;
    uint32_t num_capsets;
    uint32_t capset_ids[3];
};

/*
 * gfxstream is a process singleton and keeps the USER_DATA cookie in its
 * global log callback even after stream_renderer_teardown(), so the instance
 * handed to it is process-static: gfxstream_finish() only clears the
 * callbacks (checked by the trampolines) and a later gfxstream_init() reuses
 * the same storage. Never freed.
 */
static struct gfxstream s_instance;

/* A negative errno for a stream_renderer_* result (0 passes through). */
static int32_t GfxstreamResult(int result) {
    if (result == 0) {
        return 0;
    }
    return result < 0 ? result : -result;
}

static void GfxstreamFenceTrampoline(void* user_data, struct stream_renderer_fence* fence) {
    struct gfxstream* gfx = user_data;
    if (!gfx->builder.fence_cb) {
        return;
    }
    struct gfxstream_fence gfence = {
        .flags = fence->flags,
        .fence_id = fence->fence_id,
        .ctx_id = fence->ctx_id,
        .ring_idx = fence->ring_idx,
    };
    gfx->builder.fence_cb(gfx->builder.user_data, &gfence);
}

static void GfxstreamDebugTrampoline(void* user_data, struct stream_renderer_debug* debug) {
    struct gfxstream* gfx = user_data;
    if (!gfx->builder.debug_cb) {
        return;
    }
    struct gfxstream_debug gdebug = {
        .debug_type = debug->debug_type,
        .message = debug->message,
    };
    gfx->builder.debug_cb(gfx->builder.user_data, &gdebug);
}

static void GfxstreamDebugExTrampoline(void* user_data, struct stream_renderer_debug_ex* debug) {
    struct gfxstream* gfx = user_data;
    if (!gfx->builder.debug_cb_ex) {
        return;
    }
    struct gfxstream_debug_ex gdebug = {
        .type = debug->debug_type,
        .file = debug->file,
        .line = debug->line,
        .function = debug->function,
        .message = debug->message,
    };
    gfx->builder.debug_cb_ex(gfx->builder.user_data, &gdebug);
}

static int32_t GfxstreamInitError(char** error_msg, int32_t result, const char* fmt, ...)
        G_GNUC_PRINTF(3, 4);
static int32_t GfxstreamInitError(char** error_msg, int32_t result, const char* fmt, ...) {
    if (error_msg) {
        va_list ap;
        va_start(ap, fmt);
        *error_msg = g_strdup_vprintf(fmt, ap);
        va_end(ap);
    }
    return result;
}

int32_t gfxstream_init(const struct gfxstream_builder* builder, struct gfxstream** ptr,
                       char** error_msg) {
    char* load_error = NULL;
    int load_errno = ENOENT;
    const struct gfxstream_backend_fns* fns = gfxstream_backend_load(&load_error, &load_errno);
    struct stream_renderer_param params[8];
    uint64_t num_params = 0;
    uint32_t renderer_flags = STREAM_RENDERER_FLAGS_USE_EGL_BIT;
    struct gfxstream* gfx = &s_instance;
    int result;

    *ptr = NULL;
    if (error_msg) {
        *error_msg = NULL;
    }
    if (!fns) {
        int32_t err = GfxstreamInitError(error_msg, -(load_errno > 0 ? load_errno : ENOENT), "%s",
                                         load_error ? load_error : "failed to load backend");
        g_free(load_error);
        return err;
    }
    g_free(load_error);
    if (gfx->fns) {
        return GfxstreamInitError(error_msg, -EBUSY, "renderer already initialized");
    }
    if (builder->wsi != GFXSTREAM_WSI_SURFACELESS) {
        return GfxstreamInitError(error_msg, -EINVAL, "unsupported wsi %" PRIu64, builder->wsi);
    }
    if (!builder->fence_cb) {
        return GfxstreamInitError(error_msg, -EINVAL, "no fence callback");
    }

    memset(gfx, 0, sizeof(*gfx));
    gfx->builder = *builder;

    /* Capsets are enumerated in this fixed order, as rutabaga did. */
    if (builder->capset_mask & (1ULL << GFXSTREAM_CAPSET_VULKAN)) {
        gfx->capset_ids[gfx->num_capsets++] = GFXSTREAM_CAPSET_VULKAN;
        renderer_flags |= STREAM_RENDERER_FLAGS_USE_VK_BIT;
    }
    if (builder->capset_mask & (1ULL << GFXSTREAM_CAPSET_GLES)) {
        gfx->capset_ids[gfx->num_capsets++] = GFXSTREAM_CAPSET_GLES;
        renderer_flags |= STREAM_RENDERER_FLAGS_USE_GLES_BIT;
    }
    if (builder->capset_mask & (1ULL << GFXSTREAM_CAPSET_COMPOSER)) {
        gfx->capset_ids[gfx->num_capsets++] = GFXSTREAM_CAPSET_COMPOSER;
    }
    if (gfx->num_capsets == 0) {
        /*
         * rutabaga fell back to a 2D-only device here; the launcher always
         * enables at least gfxstream-vulkan, so this is a configuration error.
         */
        memset(gfx, 0, sizeof(*gfx));
        return GfxstreamInitError(error_msg, -EINVAL,
                                  "no capset enabled (need gfxstream-vulkan, x-gfxstream-gles or "
                                  "x-gfxstream-composer)");
    }

    params[num_params++] = (struct stream_renderer_param){STREAM_RENDERER_PARAM_USER_DATA,
                                                          (uint64_t)(uintptr_t)gfx};
    params[num_params++] =
            (struct stream_renderer_param){STREAM_RENDERER_PARAM_RENDERER_FLAGS, renderer_flags};
    params[num_params++] = (struct stream_renderer_param){
        STREAM_RENDERER_PARAM_FENCE_CALLBACK, (uint64_t)(uintptr_t)GfxstreamFenceTrampoline};
    params[num_params++] = (struct stream_renderer_param){
        STREAM_RENDERER_PARAM_WIN0_WIDTH, builder->display_width ? builder->display_width : 1280};
    params[num_params++] = (struct stream_renderer_param){
        STREAM_RENDERER_PARAM_WIN0_HEIGHT,
        builder->display_height ? builder->display_height : 1024};
    if (builder->debug_cb_ex) {
        params[num_params++] =
                (struct stream_renderer_param){STREAM_RENDERER_PARAM_DEBUG_CALLBACK_EX,
                                               (uint64_t)(uintptr_t)GfxstreamDebugExTrampoline};
    } else if (builder->debug_cb) {
        params[num_params++] = (struct stream_renderer_param){
            STREAM_RENDERER_PARAM_DEBUG_CALLBACK, (uint64_t)(uintptr_t)GfxstreamDebugTrampoline};
    }
    if (builder->renderer_features && *builder->renderer_features) {
        params[num_params++] =
                (struct stream_renderer_param){STREAM_RENDERER_PARAM_RENDERER_FEATURES,
                                               (uint64_t)(uintptr_t)builder->renderer_features};
    }

    result = fns->init(params, num_params);
    if (result) {
        memset(gfx, 0, sizeof(*gfx));
        return GfxstreamInitError(error_msg, GfxstreamResult(result),
                                  "stream_renderer_init failed (%d)", result);
    }
    gfx->fns = fns;
    *ptr = gfx;
    return 0;
}

int32_t gfxstream_finish(struct gfxstream** ptr) {
    struct gfxstream* gfx = *ptr;
    if (!gfx) {
        return -EINVAL;
    }
    gfx->fns->teardown();
    /* Keep the storage (see s_instance); drop the callbacks and the table. */
    memset(gfx, 0, sizeof(*gfx));
    *ptr = NULL;
    return 0;
}

int32_t gfxstream_get_num_capsets(struct gfxstream* ptr, uint32_t* num_capsets) {
    *num_capsets = ptr->num_capsets;
    return 0;
}

int32_t gfxstream_get_capset_info(struct gfxstream* ptr, uint32_t capset_index, uint32_t* capset_id,
                                  uint32_t* capset_version, uint32_t* capset_size) {
    if (capset_index >= ptr->num_capsets) {
        return -EINVAL;
    }
    *capset_id = ptr->capset_ids[capset_index];
    /* gfxstream never writes the version; rutabaga reported 0 as well. */
    *capset_version = 0;
    *capset_size = 0;
    ptr->fns->get_cap_set(*capset_id, capset_version, capset_size);
    return 0;
}

int32_t gfxstream_get_capset(struct gfxstream* ptr, uint32_t capset_id, uint32_t version,
                             uint8_t* capset, uint32_t capset_size) {
    uint32_t max_version = 0;
    uint32_t max_size = 0;
    ptr->fns->get_cap_set(capset_id, &max_version, &max_size);
    if (capset_size < max_size) {
        return -EINVAL;
    }
    ptr->fns->fill_caps(capset_id, version, capset);
    return 0;
}

int32_t gfxstream_context_create(struct gfxstream* ptr, uint32_t ctx_id, uint32_t context_init,
                                 const char* context_name, uint32_t context_name_len) {
    if (!context_name || context_name_len == 0 || context_name[0] == '\0') {
        context_name = GFXSTREAM_DEFAULT_CONTEXT_NAME;
        context_name_len = (uint32_t)strlen(GFXSTREAM_DEFAULT_CONTEXT_NAME);
    } else {
        context_name_len = (uint32_t)strnlen(context_name, context_name_len);
    }
    return GfxstreamResult(
            ptr->fns->context_create(ctx_id, context_name_len, context_name, context_init));
}

int32_t gfxstream_context_destroy(struct gfxstream* ptr, uint32_t ctx_id) {
    ptr->fns->context_destroy(ctx_id);
    return 0;
}

int32_t gfxstream_context_attach_resource(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id) {
    ptr->fns->ctx_attach_resource((int)ctx_id, (int)resource_id);
    return 0;
}

int32_t gfxstream_context_detach_resource(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id) {
    ptr->fns->ctx_detach_resource((int)ctx_id, (int)resource_id);
    return 0;
}

int32_t gfxstream_resource_create_3d(struct gfxstream* ptr, uint32_t resource_id,
                                     const struct gfxstream_create_3d* create_3d) {
    struct stream_renderer_resource_create_args args = {
        .handle = resource_id,
        .target = create_3d->target,
        .format = create_3d->format,
        .bind = create_3d->bind,
        .width = create_3d->width,
        .height = create_3d->height,
        .depth = create_3d->depth,
        .array_size = create_3d->array_size,
        .last_level = create_3d->last_level,
        .nr_samples = create_3d->nr_samples,
        .flags = create_3d->flags,
    };
    return GfxstreamResult(ptr->fns->resource_create(&args, NULL, 0));
}

int32_t gfxstream_resource_attach_backing(struct gfxstream* ptr, uint32_t resource_id,
                                          const struct gfxstream_iovecs* iovecs) {
    return GfxstreamResult(ptr->fns->resource_attach_iov((int)resource_id, iovecs->iovecs,
                                                         (int)iovecs->num_iovecs));
}

int32_t gfxstream_resource_detach_backing(struct gfxstream* ptr, uint32_t resource_id) {
    ptr->fns->resource_detach_iov((int)resource_id, NULL, NULL);
    return 0;
}

static struct stream_renderer_box GfxstreamBox(const struct gfxstream_transfer* transfer) {
    struct stream_renderer_box box = {
        .x = transfer->x,
        .y = transfer->y,
        .z = transfer->z,
        .w = transfer->w,
        .h = transfer->h,
        .d = transfer->d,
    };
    return box;
}

int32_t gfxstream_resource_transfer_read(struct gfxstream* ptr, uint32_t ctx_id,
                                         uint32_t resource_id,
                                         const struct gfxstream_transfer* transfer,
                                         const struct iovec* iovec) {
    struct stream_renderer_box box = GfxstreamBox(transfer);
    if (box.w == 0 || box.h == 0 || box.d == 0) {
        return 0;
    }
    return GfxstreamResult(ptr->fns->transfer_read_iov(
            resource_id, ctx_id, transfer->level, transfer->stride, transfer->layer_stride, &box,
            transfer->offset, (struct iovec*)iovec, iovec ? 1 : 0));
}

int32_t gfxstream_resource_transfer_write(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id,
                                          const struct gfxstream_transfer* transfer) {
    return gfxstream_resource_transfer_write_goldfish(ptr, ctx_id, resource_id, transfer, NULL);
}

int32_t gfxstream_resource_transfer_write_goldfish(struct gfxstream* ptr, uint32_t ctx_id,
                                                   uint32_t resource_id,
                                                   const struct gfxstream_transfer* transfer,
                                                   const struct iovec* iovec) {
    struct stream_renderer_box box = GfxstreamBox(transfer);
    if (box.w == 0 || box.h == 0 || box.d == 0) {
        return 0;
    }
    return GfxstreamResult(ptr->fns->transfer_write_iov(
            resource_id, ctx_id, (int)transfer->level, transfer->stride, transfer->layer_stride,
            &box, transfer->offset, (struct iovec*)iovec, iovec ? 1 : 0));
}

int32_t gfxstream_resource_create_blob(struct gfxstream* ptr, uint32_t ctx_id, uint32_t resource_id,
                                       const struct gfxstream_create_blob* create_blob,
                                       const struct gfxstream_iovecs* iovecs,
                                       const struct gfxstream_handle* handle) {
    struct stream_renderer_create_blob blob = {
        .blob_mem = create_blob->blob_mem,
        .blob_flags = create_blob->blob_flags,
        .blob_id = create_blob->blob_id,
        .size = create_blob->size,
    };
    if (handle) {
        return -EINVAL; /* imports are not used by the device */
    }
    return GfxstreamResult(ptr->fns->create_blob(ctx_id, resource_id, &blob,
                                                 iovecs ? iovecs->iovecs : NULL,
                                                 iovecs ? (uint32_t)iovecs->num_iovecs : 0, NULL));
}

int32_t gfxstream_resource_unref(struct gfxstream* ptr, uint32_t resource_id) {
    ptr->fns->resource_unref(resource_id);
    return 0;
}

int32_t gfxstream_resource_map(struct gfxstream* ptr, uint32_t resource_id,
                               struct gfxstream_mapping* mapping) {
    return GfxstreamResult(ptr->fns->resource_map(resource_id, &mapping->ptr, &mapping->size));
}

int32_t gfxstream_resource_unmap(struct gfxstream* ptr, uint32_t resource_id) {
    return GfxstreamResult(ptr->fns->resource_unmap(resource_id));
}

int32_t gfxstream_resource_map_info(struct gfxstream* ptr, uint32_t resource_id,
                                    uint32_t* map_info) {
    return GfxstreamResult(ptr->fns->resource_map_info(resource_id, map_info));
}

int32_t gfxstream_submit_command(struct gfxstream* ptr, struct gfxstream_command* cmd) {
    struct stream_renderer_command srcmd = {
        .ctx_id = cmd->ctx_id,
        .cmd_size = cmd->cmd_size,
        .cmd = cmd->cmd,
        .num_in_fences = 0,
        .fences = NULL,
    };
    if (cmd->cmd_size % 4 != 0) {
        return -EINVAL;
    }
    return GfxstreamResult(ptr->fns->submit_cmd(&srcmd));
}

int32_t gfxstream_create_fence(struct gfxstream* ptr, const struct gfxstream_fence* fence) {
    struct stream_renderer_fence srfence = {
        .flags = fence->flags,
        .fence_id = fence->fence_id,
        .ctx_id = fence->ctx_id,
        .ring_idx = (uint8_t)fence->ring_idx,
    };
    /*
     * gfxstream never queues work on ring 1; rutabaga signaled such fences
     * from the caller without creating a timeline for them.
     */
    if ((fence->flags & GFXSTREAM_FLAG_INFO_RING_IDX) && fence->ring_idx == 1) {
        ptr->builder.fence_cb(ptr->builder.user_data, fence);
        return 0;
    }
    return GfxstreamResult(ptr->fns->create_fence(&srfence));
}

int32_t gfxstream_snapshot(struct gfxstream* ptr, const char* dir) {
    char* subdir = g_build_filename(dir, GFXSTREAM_SNAPSHOT_SUBDIR, NULL);
    if (g_mkdir_with_parents(subdir, 0755) == -1) {
        int saved_errno = errno;
        g_free(subdir);
        return -saved_errno;
    }
    int32_t res = GfxstreamResult(ptr->fns->snapshot(subdir));
    g_free(subdir);
    return res;
}

int32_t gfxstream_restore(struct gfxstream* ptr, const char* dir) {
    char* subdir = g_build_filename(dir, GFXSTREAM_SNAPSHOT_SUBDIR, NULL);
    int32_t res = GfxstreamResult(ptr->fns->restore(subdir));
    g_free(subdir);
    return res;
}
