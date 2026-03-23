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

static const DisplayChangeListenerOps k_dcl_ops = {
    .dpy_name = "grpc-display",
    .dpy_gfx_update = grpc_dpy_gfx_update,
    .dpy_gfx_switch = grpc_dpy_gfx_switch,
};

static QLIST_HEAD(, DisplayChangeListener) s_dcls = QLIST_HEAD_INITIALIZER(DisplayChangeListener);

static void android_display_init(struct DisplayState* ds, struct DisplayOptions* o) {
    for (int idx = 0;; idx++) {
        QemuConsole* con = qemu_console_lookup_by_index(idx);
        if (!con) {
            break;
        }
        if (!qemu_console_is_graphic(con)) {
            continue;
        }

        DisplayChangeListener* dcl = g_malloc0(sizeof(DisplayChangeListener));
        if (!dcl) {
            break;
        }

        dcl->con = con;
        dcl->ops = &k_dcl_ops;

        grpc_dpy_gfx_update_ui_info(con, 0, 0);

        register_displaychangelistener(dcl);
    }
}

static QemuDisplay qemu_display_android = {
    .type = DISPLAY_TYPE_ANDROID,
    .init = android_display_init,
};

void grpc_display_register() {
    qemu_display_register(&qemu_display_android);
}
