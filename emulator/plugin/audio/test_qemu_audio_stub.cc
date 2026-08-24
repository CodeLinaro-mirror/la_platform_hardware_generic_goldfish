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

#include <cstddef>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

// NOLINTBEGIN(readability-identifier-naming)
struct AudioBackend {
    int dummy;
};

struct CaptureVoiceOut {
    void* opaque;
    void (*notify)(void* opaque, int cmd);
    void (*capture)(void* opaque, const void* buf, int size);
    void (*destroy)(void* opaque);
};

struct AudioCaptureOps {
    void (*notify)(void* opaque, int cmd);
    void (*capture)(void* opaque, const void* buf, int size);
    void (*destroy)(void* opaque);
};

namespace {

AudioBackend g_default_be;
absl::Mutex g_audio_stub_mutex;
int g_fail_add_capture ABSL_GUARDED_BY(g_audio_stub_mutex) = 0;

CaptureVoiceOut g_cap_voice ABSL_GUARDED_BY(g_audio_stub_mutex);
test_capture_state_cb g_state_cb ABSL_GUARDED_BY(g_audio_stub_mutex) = nullptr;
void* g_state_user_data ABSL_GUARDED_BY(g_audio_stub_mutex) = nullptr;

}  // namespace

extern "C" {

void test_set_capture_state_callback(test_capture_state_cb cb, void* user_data) {
    absl::MutexLock lock(&g_audio_stub_mutex);
    g_state_cb = cb;
    g_state_user_data = user_data;
}

bool bql_locked() {
    return true;
}

void bql_lock_impl(const char* /*file*/, int /*line*/) {}

void bql_unlock() {}

AudioBackend* audio_get_default_audio_be(void* /*errp*/) {
    return &g_default_be;
}

AudioBackend* audio_be_by_name(const char* /*name*/, void* /*errp*/) {
    return &g_default_be;
}

CaptureVoiceOut* audio_be_add_capture(AudioBackend* be, const void* /*as*/, const void* ops_ptr,
                                      void* cb_opaque) ABSL_NO_THREAD_SAFETY_ANALYSIS {
    const auto* ops = static_cast<const AudioCaptureOps*>(ops_ptr);

    test_capture_state_cb cb = nullptr;
    void* user_data = nullptr;
    {
        absl::MutexLock lock(&g_audio_stub_mutex);
        if (!be || !ops || g_fail_add_capture != 0) {
            return nullptr;
        }
        g_cap_voice.opaque = cb_opaque;
        g_cap_voice.notify = ops->notify;
        g_cap_voice.capture = ops->capture;
        g_cap_voice.destroy = ops->destroy;
        cb = g_state_cb;
        user_data = g_state_user_data;
    }

    if (cb) {
        cb(1, user_data);
    }
    return &g_cap_voice;
}

void audio_be_del_capture(AudioBackend* /*be*/, CaptureVoiceOut* cap, void* cb_opaque) {
    if (!cap) {
        return;
    }
    void (*destroy_fn)(void*) = nullptr;
    test_capture_state_cb cb = nullptr;
    void* user_data = nullptr;
    {
        absl::MutexLock lock(&g_audio_stub_mutex);
        if (cap->destroy) {
            destroy_fn = cap->destroy;
        }
        cap->opaque = nullptr;
        cap->notify = nullptr;
        cap->capture = nullptr;
        cap->destroy = nullptr;
        cb = g_state_cb;
        user_data = g_state_user_data;
    }

    if (destroy_fn) {
        destroy_fn(cb_opaque);
    }
    if (cb) {
        cb(0, user_data);
    }
}

void test_simulate_qemu_audio_output(const void* buf, int size) {
    void (*capture_fn)(void*, const void*, int) = nullptr;
    void* opaque = nullptr;
    {
        absl::MutexLock lock(&g_audio_stub_mutex);
        if (g_cap_voice.capture) {
            capture_fn = g_cap_voice.capture;
            opaque = g_cap_voice.opaque;
        }
    }

    if (capture_fn) {
        capture_fn(opaque, buf, size);
    }
}

void test_simulate_qemu_notify(int cmd) {
    void (*notify_fn)(void*, int) = nullptr;
    void* opaque = nullptr;
    {
        absl::MutexLock lock(&g_audio_stub_mutex);
        if (g_cap_voice.notify) {
            notify_fn = g_cap_voice.notify;
            opaque = g_cap_voice.opaque;
        }
    }

    if (notify_fn) {
        notify_fn(opaque, cmd);
    }
}

int test_has_active_capture() {
    absl::MutexLock lock(&g_audio_stub_mutex);
    return (g_cap_voice.capture != nullptr) ? 1 : 0;
}

void test_reset_audio_stubs() {
    absl::MutexLock lock(&g_audio_stub_mutex);
    g_cap_voice.opaque = nullptr;
    g_cap_voice.notify = nullptr;
    g_cap_voice.capture = nullptr;
    g_cap_voice.destroy = nullptr;
    g_fail_add_capture = 0;
    g_state_cb = nullptr;
    g_state_user_data = nullptr;
}

void test_set_fail_add_capture(int fail) {
    absl::MutexLock lock(&g_audio_stub_mutex);
    g_fail_add_capture = fail;
}

}  // extern "C"
// NOLINTEND(readability-identifier-naming)
