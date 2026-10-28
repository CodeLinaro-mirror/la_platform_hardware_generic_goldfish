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

// What fake_gfxstream_backend.c records about the stream_renderer_* calls it
// receives. Tests reach it through g_module_symbol("fake_gfxstream_state") on
// the already loaded fake library (gfxstream_renderer_test.cc).

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stream_renderer_fence;
struct stream_renderer_debug_ex;

struct fake_gfxstream_state {
    // stream_renderer_init: how often, and the parameters of the last call.
    int init_calls;
    int teardown_calls;
    uint64_t user_data;
    uint64_t renderer_flags;
    uint64_t win0_width;
    uint64_t win0_height;
    void (*fence_callback)(void* user_data, struct stream_renderer_fence* fence);
    void (*debug_callback_ex)(void* user_data, struct stream_renderer_debug_ex* debug);
    char renderer_features[256];

    // Last stream_renderer_context_create.
    int context_create_calls;
    uint32_t context_id;
    uint32_t context_name_len;
    char context_name[64];
    uint32_t context_init;

    int submit_cmd_calls;
    uint32_t submit_cmd_size;

    int transfer_read_calls;
    int transfer_write_calls;
    int transfer_iovec_cnt;
    uint32_t transfer_box_w;

    int create_fence_calls;
    uint64_t fence_id;
    uint32_t fence_ring_idx;

    int snapshot_calls;
    int restore_calls;
    char snapshot_dir[1024];
    char restore_dir[1024];
};

#ifdef __cplusplus
}  // extern "C"
#endif
