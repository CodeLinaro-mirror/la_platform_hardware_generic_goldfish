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
#include "goldfish/grpc/grpc-service-device.h"

#include <stdint.h>
// clang-format off
// IWYU pragma: begin_keep

#include "qemu/typedefs.h"
#include "qemu/osdep.h"

#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "ui/console.h"

// IWYU pragma: end_keep
// clang-format on

typedef struct GrpcDev {
    DeviceClass parent_class;
    GrpcDeviceConfiguration config;
} GrpcDev;

#define TYPE_GRPC "grpc"
#define GRPC_DEV(obj) OBJECT_CHECK(GrpcDev, (obj), TYPE_GRPC)
#define GRPC_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(GrpcDev, obj, TYPE_GRPC)

// TODO(jansene): These are currently externs on Multidisplay.cpp, and should
// be injected, not linked against!
static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name = "grpc-display",
    .dpy_gfx_update = grpc_dpy_gfx_update,
    .dpy_gfx_switch = grpc_dpy_gfx_switch,
};

static DisplayChangeListener dcl = {
    .ops = &dcl_ops,
};

static void android_display_init(struct DisplayState* ds, struct DisplayOptions* o) {
    //  android_display_init has been called! Time t
    QemuConsole* con;
    for (int idx = 0;; idx++) {
        con = qemu_console_lookup_by_index(idx);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }

        // Note we expect our gRPC handler to do figure out
        // console --> display mapping.
        register_displaychangelistener(&dcl);
    }
}

static void android_display_early_init(struct DisplayOptions* o) {
    // Very early on, we likely do not care about this
}

static QemuDisplay qemu_display_android = {
    .type = DISPLAY_TYPE_ANDROID,
    .early_init = android_display_early_init,
    .init = android_display_init,
};

static void grpc_realize(DeviceState* dev, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(dev);
    initialize(&grpc_device->config);
}

static void grpc_unrealize(DeviceState* dev) {
    GrpcDev* grpc_device = GRPC_DEV(dev);
    finalize(&grpc_device->config);
}

static void grpc_set_tls_cer(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.tls_cer = malloc(strlen(value) + 1);
    strcpy(grpc_device->config.tls_cer, value);
}

static void grpc_set_tls_key(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.tls_key = malloc(strlen(value) + 1);
    strcpy(grpc_device->config.tls_key, value);
}

static void grpc_set_tls_ca(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.tls_ca = malloc(strlen(value) + 1);
    strcpy(grpc_device->config.tls_ca, value);
}

static void grpc_set_allowlist(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.allowlist = malloc(strlen(value) + 1);
    strcpy(grpc_device->config.allowlist, value);
}

static void grpc_set_addr(Object* obj, const char* value, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.addr = malloc(strlen(value) + 1);
    strcpy(grpc_device->config.addr, value);
}

static void grpc_set_port(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    // Check for invalid input or overflow
    if (value < 0 || value > 65535) {
        error_setg(errp, "Port number should be between 0 and 65535, not: %d", value);
        return;
    }

    grpc_device->config.port = value;
}

static void grpc_set_idle_timeout(Object* obj, Visitor* v, const char* name, void* opaque,
                                  Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    grpc_device->config.idle_timeout = value;
}

static void grpc_set_enable_token(Object* obj, bool v, Error** errp) {
    GrpcDev* grpc_device = GRPC_DEV(obj);
    grpc_device->config.use_token = v;
}

static void grpc_class_init(ObjectClass* oc, void* data) {
    object_class_property_add_str(oc, "tls_cer", NULL, grpc_set_tls_cer);
    object_class_property_set_description(oc, "tls_cer",
                                          "PEM file with a X.509 public key certificate.");

    object_class_property_add_str(oc, "tls_key", NULL, grpc_set_tls_key);
    object_class_property_set_description(oc, "tls_key",
                                          "PEM file containing a private key used for TLS.");

    object_class_property_add_str(oc, "tls_ca", NULL, grpc_set_tls_ca);
    object_class_property_set_description(
            oc, "tls_ca",
            "PEM file containing a series of certificate authorities for client "
            "validation.");

    object_class_property_add_str(oc, "allowlist", NULL, grpc_set_allowlist);
    object_class_property_set_description(oc, "allowlist",
                                          "A json file describing the access rules.");

    object_class_property_add_str(oc, "addr", NULL, grpc_set_addr);
    object_class_property_set_description(oc, "addr",
                                          "Address to which the grpc service should be bound.");

    object_class_property_add(oc, "port", "int", NULL, grpc_set_port, NULL, NULL);
    object_class_property_set_description(oc, "port",
                                          "The port to which the grpc service should be bound.");

    object_class_property_add(oc, "idle_timeout", "int", NULL, grpc_set_idle_timeout, NULL, NULL);
    object_class_property_set_description(
            oc, "idle_timeout",
            "Shutdown the emulator after idle_timeout seconds of inactivity from the "
            "gRPC endpoint.");

    object_class_property_add_bool(oc, "token", NULL, grpc_set_enable_token);
    object_class_property_set_description(oc, "token",
                                          "Require an authorization header with "
                                          "a valid token for every grpc call.");

    qemu_display_register(&qemu_display_android);
    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = grpc_realize;
    dc->unrealize = grpc_unrealize;
}

static const TypeInfo grpc_type_info = {
    .name = TYPE_GRPC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(GrpcDev),
    .class_init = grpc_class_init,
};

void grpc_register_types(void) {
    type_register_static(&grpc_type_info);
}
