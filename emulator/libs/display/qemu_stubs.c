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
#include <time.h>
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-input.h"
// clang-format on

// A set of stubs for qemu methods we do not have.
void qemu_input_event_sync(void) {}
void warn_report_err(Error* err) {
    (void)err;
}
void qemu_input_update_buttons(QemuConsole* src, uint32_t* button_map,  // NOLINT
                               uint32_t button_old, uint32_t button_new) {
    (void)src;
    (void)button_map;
    (void)button_old;
    (void)button_new;
}
void qemu_input_queue_abs(QemuConsole* src, InputAxis axis, int value, int min_in, int max_in) {
    (void)src;
    (void)axis;
    (void)value;
    (void)min_in;
    (void)max_in;
}

QemuConsole* qemu_console_lookup_default() {
    return NULL;
}
uint32_t qemu_console_get_head(QemuConsole* con) {
    (void)con;
    return 0;
}
int qemu_console_get_index(QemuConsole* con) {
    (void)con;
    return 0;
}

Object* object_property_get_link(Object* obj, const char* name, Error** errp) {
    (void)obj;
    (void)name;
    (void)errp;
    return NULL;
}

Object* object_get_root(void) {
    return NULL;
}
Object* object_dynamic_cast_assert(Object* obj, const char* type_name, const char* file, int line,
                                   const char* func) {
    (void)obj;
    (void)type_name;
    (void)file;
    (void)line;
    (void)func;
    return NULL;
}

Object* object_dynamic_cast(Object* obj, const char* type_name) {
    (void)obj;
    (void)type_name;
    return NULL;
}

int object_child_foreach_recursive(Object* obj, int (*fn)(Object* child, void* opaque),
                                   void* opaque) {
    (void)obj;
    (void)fn;
    (void)opaque;
    return 0;
}

Object* object_resolve_path_component(Object* parent, const char* part) {
    (void)parent;
    (void)part;
    return NULL;
}

Object* object_resolve_path_type(const char* path, const char* type_name,
                                 bool* ambiguous) {  // NOLINT(readability-non-const-parameter)
    (void)path;
    (void)type_name;
    (void)ambiguous;
    return NULL;
}

void virtio_input_send(VirtIOInput* vinput, virtio_input_event* event) {
    (void)vinput;
    (void)event;
}
void console_handle_touch_event(QemuConsole* con,
                                struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX],
                                uint64_t num_slot, int width, int height, double x, double y,
                                InputMultiTouchType type, Error** errp) {
    (void)con;
    (void)touch_slots;
    (void)num_slot;
    (void)width;
    (void)height;
    (void)x;
    (void)y;
    (void)type;
    (void)errp;
}

void qemu_free_displaysurface(DisplaySurface* surface) {
    (void)surface;
}

DisplaySurface* qemu_create_displaysurface(int width, int height) {
    (void)width;
    (void)height;
    return NULL;
}

int dpy_set_ui_info(QemuConsole* con, QemuUIInfo* info, bool delay) {
    (void)con;
    (void)info;
    (void)delay;
    return 0;
}
