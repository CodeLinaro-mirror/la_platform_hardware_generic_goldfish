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

// clang-format off
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
// clang-format on

int getMinLogLevel() {
    return 0;
}
void qemu_input_event_sync(void) {}
void warn_report_err(Error* err) {}
void qemu_input_update_buttons(QemuConsole* src, uint32_t* button_map, uint32_t button_old,
                               uint32_t button_new) {}
void qemu_input_queue_abs(QemuConsole* src, InputAxis axis, int value, int min_in, int max_in) {}
int qemu_console_get_index(QemuConsole* con) {
    return 0;
}

void console_handle_touch_event(QemuConsole* con,
                                struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX],
                                uint64_t num_slot, int width, int height, double x, double y,
                                InputMultiTouchType type, Error** errp) {}
