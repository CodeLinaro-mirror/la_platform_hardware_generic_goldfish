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

/*
 * gfxstream_* : the rutabaga_gfx C FFI shape (rutabaga_gfx_ffi.h) implemented
 * directly on libgfxstream_backend's stream_renderer_* API, so that
 * virtio_gpu_gfxstream.c can stay a near-verbatim copy of QEMU's
 * hw/display/virtio-gpu-rutabaga.c.
 *
 * Every function here maps 1:1 onto one stream_renderer_* call (see the
 * mapping table in the rutabaga removal scoping doc) plus the few behaviours
 * rutabaga_core used to add on top and QEMU must now own:
 *   - gfxstream_init() picks the capset list / renderer flags from the
 *     capset mask, defaults the window size, and dlopen()s the backend
 *     (gfxstream_backend_loader.h) on first use;
 *   - gfxstream_get_num_capsets() / gfxstream_get_capset_info() keep the
 *     capset index table (VULKAN, GLES, COMPOSER order);
 *   - gfxstream_context_create() defaults an empty name to "gpu_renderer";
 *   - gfxstream_submit_command() rejects cmd_size % 4 != 0;
 *   - gfxstream_resource_transfer_{read,write}() return success for an
 *     empty box without calling the renderer;
 *   - gfxstream_create_fence() completes ring_idx == 1 fences synchronously
 *     without involving the renderer;
 *   - gfxstream_snapshot() / gfxstream_restore() use the <dir>/gfxstream/
 *     sub-directory rutabaga used (there is no rutabaga_snapshot JSON).
 * Duplicate/unknown resource and context ids are validated by the device
 * against its own tables (gfxstream silently overwrites table entries).
 *
 * Callbacks keep rutabaga's (uint64_t user_data, const struct ...*) shape;
 * the layer registers trampolines with the renderer and converts.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec;

/* Capset ids: identical to rutabaga's and to the virtio-gpu spec values. */
#define GFXSTREAM_CAPSET_VULKAN 3
#define GFXSTREAM_CAPSET_CROSS_DOMAIN 5 /* never advertised; kept for the snapshot check */
#define GFXSTREAM_CAPSET_GLES 8
#define GFXSTREAM_CAPSET_COMPOSER 9

/* Mapping info, same bits as RUTABAGA_MAP_* / STREAM_RENDERER_MAP_*. */
#define GFXSTREAM_MAP_CACHE_MASK 0x0f

/* The only WSI the renderer supports. */
#define GFXSTREAM_WSI_SURFACELESS 0x1

/* Fence flags, same bits as RUTABAGA_FLAG_* / STREAM_RENDERER_FLAG_*. */
#define GFXSTREAM_FLAG_FENCE (1 << 0)
#define GFXSTREAM_FLAG_INFO_RING_IDX (1 << 1)

/* Debug levels, same values as RUTABAGA_DEBUG_* / STREAM_RENDERER_DEBUG_*. */
#define GFXSTREAM_DEBUG_ERROR 0x1
#define GFXSTREAM_DEBUG_WARN 0x2
#define GFXSTREAM_DEBUG_INFO 0x3
#define GFXSTREAM_DEBUG_DEBUG 0x4

struct gfxstream;

struct gfxstream_create_blob {
    uint32_t blob_mem;
    uint32_t blob_flags;
    uint64_t blob_id;
    uint64_t size;
};

struct gfxstream_create_3d {
    uint32_t target;
    uint32_t format;
    uint32_t bind;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
};

struct gfxstream_transfer {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
    uint64_t offset;
};

struct gfxstream_iovecs {
    struct iovec* iovecs;
    size_t num_iovecs;
};

struct gfxstream_handle {
    int64_t os_handle;
    uint32_t handle_type;
};

struct gfxstream_mapping {
    void* ptr;
    uint64_t size;
};

struct gfxstream_command {
    uint32_t ctx_id;
    uint32_t cmd_size;
    uint8_t* cmd;
    uint32_t num_in_fences;
    uint64_t* fence_ids;
};

struct gfxstream_fence {
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t ring_idx;
};

struct gfxstream_debug {
    uint32_t debug_type;
    const char* message;
};

struct gfxstream_debug_ex {
    uint32_t type;        /* GFXSTREAM_DEBUG_* */
    const char* file;     /* The source file where the log was generated. */
    int line;             /* The line number in the source file. */
    const char* function; /* The function name where the log was generated. */
    const char* message;  /* The actual log message. */
};

typedef void (*gfxstream_fence_callback)(uint64_t user_data, const struct gfxstream_fence* fence);
typedef void (*gfxstream_debug_callback)(uint64_t user_data, const struct gfxstream_debug* debug);
typedef void (*gfxstream_debug_callback_ex)(uint64_t user_data,
                                            const struct gfxstream_debug_ex* debug);

struct gfxstream_builder {
    uint64_t user_data;
    uint64_t capset_mask;
    uint64_t wsi;
    gfxstream_fence_callback fence_cb;
    gfxstream_debug_callback debug_cb;
    gfxstream_debug_callback_ex debug_cb_ex;
    const char* renderer_features;
    uint32_t display_width;
    uint32_t display_height;
};

/* All functions return 0 on success and a negative errno on failure. */

/*
 * On failure *error_msg (if non-NULL) receives a g_malloc()ed description
 * (library load error, missing symbols, no capset enabled, ...); the caller
 * g_free()s it. Only one instance can exist at a time (gfxstream is a process
 * singleton); gfxstream_finish() it before calling gfxstream_init() again.
 */
int32_t gfxstream_init(const struct gfxstream_builder* builder, struct gfxstream** ptr,
                       char** error_msg);
int32_t gfxstream_finish(struct gfxstream** ptr);

int32_t gfxstream_get_num_capsets(struct gfxstream* ptr, uint32_t* num_capsets);
int32_t gfxstream_get_capset_info(struct gfxstream* ptr, uint32_t capset_index, uint32_t* capset_id,
                                  uint32_t* capset_version, uint32_t* capset_size);
int32_t gfxstream_get_capset(struct gfxstream* ptr, uint32_t capset_id, uint32_t version,
                             uint8_t* capset, uint32_t capset_size);

int32_t gfxstream_context_create(struct gfxstream* ptr, uint32_t ctx_id, uint32_t context_init,
                                 const char* context_name, uint32_t context_name_len);
int32_t gfxstream_context_destroy(struct gfxstream* ptr, uint32_t ctx_id);
int32_t gfxstream_context_attach_resource(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id);
int32_t gfxstream_context_detach_resource(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id);

int32_t gfxstream_resource_create_3d(struct gfxstream* ptr, uint32_t resource_id,
                                     const struct gfxstream_create_3d* create_3d);
int32_t gfxstream_resource_attach_backing(struct gfxstream* ptr, uint32_t resource_id,
                                          const struct gfxstream_iovecs* iovecs);
int32_t gfxstream_resource_detach_backing(struct gfxstream* ptr, uint32_t resource_id);
int32_t gfxstream_resource_transfer_read(struct gfxstream* ptr, uint32_t ctx_id,
                                         uint32_t resource_id,
                                         const struct gfxstream_transfer* transfer,
                                         const struct iovec* iovec);
int32_t gfxstream_resource_transfer_write(struct gfxstream* ptr, uint32_t ctx_id,
                                          uint32_t resource_id,
                                          const struct gfxstream_transfer* transfer);
/* Host-initiated write from an explicit iovec (the rutabaga_gfx_ffi_goldfish.h
 * extension virtio_gpu_glue uses); the device itself never calls this. */
int32_t gfxstream_resource_transfer_write_goldfish(struct gfxstream* ptr, uint32_t ctx_id,
                                                   uint32_t resource_id,
                                                   const struct gfxstream_transfer* transfer,
                                                   const struct iovec* iovec);
int32_t gfxstream_resource_create_blob(struct gfxstream* ptr, uint32_t ctx_id, uint32_t resource_id,
                                       const struct gfxstream_create_blob* create_blob,
                                       const struct gfxstream_iovecs* iovecs,
                                       const struct gfxstream_handle* handle);
int32_t gfxstream_resource_unref(struct gfxstream* ptr, uint32_t resource_id);
int32_t gfxstream_resource_map(struct gfxstream* ptr, uint32_t resource_id,
                               struct gfxstream_mapping* mapping);
int32_t gfxstream_resource_unmap(struct gfxstream* ptr, uint32_t resource_id);
int32_t gfxstream_resource_map_info(struct gfxstream* ptr, uint32_t resource_id,
                                    uint32_t* map_info);

int32_t gfxstream_submit_command(struct gfxstream* ptr, struct gfxstream_command* cmd);
int32_t gfxstream_create_fence(struct gfxstream* ptr, const struct gfxstream_fence* fence);

int32_t gfxstream_snapshot(struct gfxstream* ptr, const char* dir);
int32_t gfxstream_restore(struct gfxstream* ptr, const char* dir);

#ifdef __cplusplus
}  // extern "C"
#endif
