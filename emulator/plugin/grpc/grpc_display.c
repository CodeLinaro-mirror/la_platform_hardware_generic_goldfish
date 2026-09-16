// Copyright 2024 The Android Open Source Project
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

#include "emulator/plugin/grpc/grpc_display.h"

#include "goldfish/display/multi_display_callbacks.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
// IWYU pragma: end_keep
// clang-format on

static void android_display_init(struct DisplayState* ds, struct DisplayOptions* o) {
    for (int idx = 0;; idx++) {
        QemuConsole* con = qemu_console_lookup_by_index(idx);
        if (!con) {
            break;
        }
        if (!qemu_console_is_graphic(con)) {
            continue;
        }

        // UI info must still be initialized. DCL registration is handled dynamically by
        // QemuDisplay.
        grpc_dpy_gfx_update_ui_info(con, 0, 0);

        // Eagerly push the initial surface to multi_display so QemuDisplay instances
        // are created. We pass a dummy DCL struct because grpc_dpy_gfx_switch expects one.
        DisplayChangeListener dummy_dcl = {.con = con};
        grpc_dpy_gfx_switch(&dummy_dcl, qemu_console_surface(con));
    }
}

static QemuDisplay qemu_display_android = {
    .type = DISPLAY_TYPE_ANDROID,
    .init = android_display_init,
};

void grpc_display_register() {
    qemu_display_register(&qemu_display_android);
}
