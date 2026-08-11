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
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*test_capture_state_cb)(int active, void* user_data);
void test_set_capture_state_callback(test_capture_state_cb cb, void* user_data);

void test_simulate_qemu_audio_output(const void* buf, int size);
void test_simulate_qemu_notify(int cmd);
int test_has_active_capture(void);
void test_reset_audio_stubs(void);
void test_set_fail_add_capture(int fail);

#ifdef __cplusplus
}
#endif
