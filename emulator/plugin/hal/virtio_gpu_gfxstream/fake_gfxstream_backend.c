// Copyright 2026 The Android Open Source Project
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

// A stand-in for libgfxstream_backend that exports every stream_renderer_*
// entry point gfxstream_backend_loader.c resolves, so the loader and the
// gfxstream_* compat layer can be tested without a GPU. Built as
// libgfxstream_backend.{so,dylib} next to the tests; every call is recorded in
// fake_gfxstream_state (fake_gfxstream_backend.h).

#include "fake_gfxstream_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"

extern VG_EXPORT struct fake_gfxstream_state fake_gfxstream_state;
VG_EXPORT void fake_gfxstream_reset(void);  // NOLINT(readability-identifier-naming)

VG_EXPORT struct fake_gfxstream_state fake_gfxstream_state;

VG_EXPORT void fake_gfxstream_reset(void) {  // NOLINT(readability-identifier-naming)
    memset(&fake_gfxstream_state, 0, sizeof(fake_gfxstream_state));
}

VG_EXPORT int stream_renderer_init(struct stream_renderer_param* params, uint64_t num_params) {
    struct fake_gfxstream_state* st = &fake_gfxstream_state;
    st->init_calls++;
    for (uint64_t i = 0; i < num_params; i++) {
        switch (params[i].key) {
        case STREAM_RENDERER_PARAM_USER_DATA:
            st->user_data = params[i].value;
            break;
        case STREAM_RENDERER_PARAM_RENDERER_FLAGS:
            st->renderer_flags = params[i].value;
            break;
        case STREAM_RENDERER_PARAM_WIN0_WIDTH:
            st->win0_width = params[i].value;
            break;
        case STREAM_RENDERER_PARAM_WIN0_HEIGHT:
            st->win0_height = params[i].value;
            break;
        case STREAM_RENDERER_PARAM_FENCE_CALLBACK:
            st->fence_callback =
                    (void (*)(void*, struct stream_renderer_fence*))(uintptr_t)params[i].value;
            break;
        case STREAM_RENDERER_PARAM_DEBUG_CALLBACK_EX:
            st->debug_callback_ex =
                    (void (*)(void*, struct stream_renderer_debug_ex*))(uintptr_t)params[i].value;
            break;
        case STREAM_RENDERER_PARAM_RENDERER_FEATURES:
            snprintf(st->renderer_features, sizeof(st->renderer_features), "%s",
                     (const char*)(uintptr_t)params[i].value);
            break;
        default:
            break;
        }
    }
    return 0;
}
VG_EXPORT void stream_renderer_teardown(void) {
    fake_gfxstream_state.teardown_calls++;
}
VG_EXPORT int stream_renderer_resource_create(struct stream_renderer_resource_create_args* args,
                                              struct iovec* iov, uint32_t num_iovs) {
    (void)args;
    (void)iov;
    (void)num_iovs;
    return 0;
}
VG_EXPORT void stream_renderer_resource_unref(uint32_t res_handle) {
    (void)res_handle;
}
VG_EXPORT int stream_renderer_context_create(uint32_t ctx_id, uint32_t nlen, const char* name,
                                             uint32_t context_init) {
    struct fake_gfxstream_state* st = &fake_gfxstream_state;
    st->context_create_calls++;
    st->context_id = ctx_id;
    st->context_name_len = nlen;
    st->context_init = context_init;
    memset(st->context_name, 0, sizeof(st->context_name));
    memcpy(st->context_name, name,
           nlen < sizeof(st->context_name) - 1 ? nlen : sizeof(st->context_name) - 1);
    return 0;
}
VG_EXPORT void stream_renderer_context_destroy(uint32_t handle) {
    (void)handle;
}
VG_EXPORT int stream_renderer_submit_cmd(struct stream_renderer_command* cmd) {
    fake_gfxstream_state.submit_cmd_calls++;
    fake_gfxstream_state.submit_cmd_size = cmd->cmd_size;
    return 0;
}
VG_EXPORT int stream_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id, uint32_t level,
                                                uint32_t stride, uint32_t layer_stride,
                                                struct stream_renderer_box* box, uint64_t offset,
                                                struct iovec* iov, int iovec_cnt) {
    (void)handle;
    (void)ctx_id;
    (void)level;
    (void)stride;
    (void)layer_stride;
    (void)offset;
    (void)iov;
    fake_gfxstream_state.transfer_read_calls++;
    fake_gfxstream_state.transfer_iovec_cnt = iovec_cnt;
    fake_gfxstream_state.transfer_box_w = box->w;
    return 0;
}
VG_EXPORT int stream_renderer_transfer_write_iov(uint32_t handle, uint32_t ctx_id, int level,
                                                 uint32_t stride, uint32_t layer_stride,
                                                 struct stream_renderer_box* box, uint64_t offset,
                                                 struct iovec* iovec, unsigned int iovec_cnt) {
    (void)handle;
    (void)ctx_id;
    (void)level;
    (void)stride;
    (void)layer_stride;
    (void)offset;
    (void)iovec;
    fake_gfxstream_state.transfer_write_calls++;
    fake_gfxstream_state.transfer_iovec_cnt = (int)iovec_cnt;
    fake_gfxstream_state.transfer_box_w = box->w;
    return 0;
}
// Distinguishable from a real backend: reports max_ver 7, size = 4 * set.
VG_EXPORT void stream_renderer_get_cap_set(uint32_t set, uint32_t* max_ver, uint32_t* max_size) {
    *max_ver = 7;
    *max_size = 4 * set;
}
VG_EXPORT void stream_renderer_fill_caps(uint32_t set, uint32_t version, void* caps) {
    (void)version;
    memset(caps, (int)set, (size_t)4 * set);
}
VG_EXPORT int stream_renderer_resource_attach_iov(int res_handle, struct iovec* iov, int num_iovs) {
    (void)res_handle;
    (void)iov;
    (void)num_iovs;
    return 0;
}
VG_EXPORT void stream_renderer_resource_detach_iov(int res_handle, struct iovec** iov,
                                                   int* num_iovs) {
    (void)res_handle;
    (void)iov;
    (void)num_iovs;
}
VG_EXPORT void stream_renderer_ctx_attach_resource(int ctx_id, int res_handle) {
    (void)ctx_id;
    (void)res_handle;
}
VG_EXPORT void stream_renderer_ctx_detach_resource(int ctx_id, int res_handle) {
    (void)ctx_id;
    (void)res_handle;
}
VG_EXPORT int stream_renderer_create_blob(uint32_t ctx_id, uint32_t res_handle,
                                          const struct stream_renderer_create_blob* create_blob,
                                          const struct iovec* iovecs, uint32_t num_iovs,
                                          const struct stream_renderer_handle* handle) {
    (void)ctx_id;
    (void)res_handle;
    (void)create_blob;
    (void)iovecs;
    (void)num_iovs;
    (void)handle;
    return 0;
}
VG_EXPORT int stream_renderer_resource_map(uint32_t res_handle, void** hva_out,
                                           uint64_t* size_out) {
    (void)res_handle;
    *hva_out = NULL;
    *size_out = 0;
    return 0;
}
VG_EXPORT int stream_renderer_resource_unmap(uint32_t res_handle) {
    (void)res_handle;
    return 0;
}
VG_EXPORT int stream_renderer_resource_map_info(uint32_t res_handle, uint32_t* map_info) {
    (void)res_handle;
    *map_info = 0;
    return 0;
}
#ifndef GFXSTREAM_FAKE_OMIT_REQUIRED_SYMBOL
VG_EXPORT int stream_renderer_create_fence(const struct stream_renderer_fence* fence) {
    fake_gfxstream_state.create_fence_calls++;
    fake_gfxstream_state.fence_id = fence->fence_id;
    fake_gfxstream_state.fence_ring_idx = fence->ring_idx;
    return 0;
}
#endif
VG_EXPORT int stream_renderer_snapshot(const char* dir) {
    fake_gfxstream_state.snapshot_calls++;
    snprintf(fake_gfxstream_state.snapshot_dir, sizeof(fake_gfxstream_state.snapshot_dir), "%s",
             dir);
    return 0;
}
VG_EXPORT int stream_renderer_restore(const char* dir) {
    fake_gfxstream_state.restore_calls++;
    snprintf(fake_gfxstream_state.restore_dir, sizeof(fake_gfxstream_state.restore_dir), "%s", dir);
    return 0;
}
