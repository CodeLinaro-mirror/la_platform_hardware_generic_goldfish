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
#pragma once

#include "goldfish/devices/cable/cable.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
}
// IWYU pragma: end_keep
// clang-format on

typedef struct VSockProxy {
} VSockProxy;
typedef struct VSockFwdDev VSockFwdDev;

typedef void (*OnVsockHostConnectFn)(VSockFwdDev*);
typedef void (*OnVsockAcceptSocketFn)(VSockFwdDev*, goldfish::devices::cable::ISocket*);

struct VSockFwdDev {
    DeviceClass parent_class;
    int host_port;
    int guest_port;
    VSockProxy* forwarder;
    OnVsockHostConnectFn on_connect;
    OnVsockAcceptSocketFn on_accept;
};

extern "C" void vsock_port_fwd_register_types(void);

#define TYPE_VSOCK_FWD "virtio-goldfish-hostfwd-socket"
#define VSOCK_FWD_DEV(obj) OBJECT_CHECK(VSockFwdDev, (obj), TYPE_VSOCK_FWD)
#define VSOCK_FWD_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(VSockFwdDev, obj, TYPE_VSOCK_FWD)
