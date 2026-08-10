// Copyright (C) 2026 The Android Open Source Project
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

#include "test_qemu_audio_stub.h"

#include <stdbool.h>
#include <stddef.h>

// NOLINTBEGIN(readability-identifier-naming)
struct AudioBackend {
    int dummy;
};

static struct AudioBackend g_default_be;
static int g_fail_add_capture = 0;

struct CaptureVoiceOut {
    void* opaque;
    void (*notify)(void* opaque, int cmd);
    void (*capture)(void* opaque, const void* buf, int size);
    void (*destroy)(void* opaque);
};

static struct CaptureVoiceOut g_cap_voice;

bool bql_locked(void) {
    return true;
}

void bql_lock_impl(const char* file, int line) {}

void bql_unlock(void) {}

struct AudioBackend* audio_get_default_audio_be(void* errp) {
    return &g_default_be;
}

struct AudioBackend* audio_be_by_name(const char* name, void* errp) {
    return &g_default_be;
}

struct CaptureVoiceOut* audio_be_add_capture(struct AudioBackend* be, const void* as,
                                             const void* ops_ptr, void* cb_opaque) {
    if (!be || g_fail_add_capture) {
        return NULL;
    }
    const struct {
        void (*notify)(void* opaque, int cmd);
        void (*capture)(void* opaque, const void* buf, int size);
        void (*destroy)(void* opaque);
    }* ops = ops_ptr;

    g_cap_voice.opaque = cb_opaque;
    g_cap_voice.notify = ops->notify;
    g_cap_voice.capture = ops->capture;
    g_cap_voice.destroy = ops->destroy;
    return &g_cap_voice;
}

void audio_be_del_capture(struct AudioBackend* be, struct CaptureVoiceOut* cap, void* cb_opaque) {
    if (cap) {
        if (cap->destroy && cb_opaque) {
            cap->destroy(cb_opaque);
        }
        cap->opaque = NULL;
        cap->notify = NULL;
        cap->capture = NULL;
        cap->destroy = NULL;
    }
}
// NOLINTEND(readability-identifier-naming)

void test_simulate_qemu_audio_output(const void* buf, int size) {
    if (g_cap_voice.capture && g_cap_voice.opaque) {
        g_cap_voice.capture(g_cap_voice.opaque, buf, size);
    }
}

void test_simulate_qemu_notify(int cmd) {
    if (g_cap_voice.notify && g_cap_voice.opaque) {
        g_cap_voice.notify(g_cap_voice.opaque, cmd);
    }
}

int test_has_active_capture(void) {
    return (g_cap_voice.capture && g_cap_voice.opaque) ? 1 : 0;
}

void test_reset_audio_stubs(void) {
    g_cap_voice.opaque = NULL;
    g_cap_voice.notify = NULL;
    g_cap_voice.capture = NULL;
    g_cap_voice.destroy = NULL;
    g_fail_add_capture = 0;
}

void test_set_fail_add_capture(int fail) {
    g_fail_add_capture = fail;
}
