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
#include <memory>

#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"

#include "android/emulation/control/adb/AdbHostServer.h"
#include "android/emulation/control/adb/AdbMessageLogger.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/atomic.hpp"
#include "goldfish/vsock/vsock_port_fwd.h"

extern "C" {
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
}
// IWYU pragma: end_keep
// clang-format on

// Define a subclass for the VSockFwdDevice, where we are going
// to override the realize method, in the realize method we will
// override the common properties
struct AdbVSockDev {
    VSockFwdDev parent;
};

struct AdbDeviceClass {
    DeviceClass parent_class;
    DeviceRealize vsock_port_fwd_realize;
};

#define TYPE_ADB_VSOCK_DEVICE "virtio-goldfish-adb"
OBJECT_DECLARE_TYPE(AdbVSockDev, AdbDeviceClass, ADB_VSOCK_DEVICE)
#define ADB_VSOCK_DEV(obj) OBJECT_CHECK(AdbVSockDev, (obj), TYPE_ADB_VSOCK_DEVICE)
#define ADB_VSOCK_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AdbDeviceClass, obj, TYPE_ADB_VSOCK_DEVICE)

using android::emulation::AdbHostServer;
using android::emulation::control::AdbLogger;

static void adb_vsock_accept(VSockFwdDev* device, goldfish::devices::cable::ISocket* socket) {
    socket->setDataSniffer(std::make_unique<AdbLogger>(device->host_port, device->guest_port));
}

// QEMU device configuration logic
static void adb_vsock_connected(VSockFwdDev* device) {
    auto adb_server = AdbHostServer::getClientPort();
    LOG(INFO) << "Notifying adb server on port " << adb_server
              << " that adbd for is available on localhost:" << device->host_port;
    android::emulation::AdbHostServer::notify(device->host_port, adb_server);
}

static void adb_vsock_realize(DeviceState* dev, Error** errp) {
    VSockFwdDev* vsock_fwd_dev = VSOCK_FWD_DEV(dev);
    auto adc = ADB_VSOCK_DEVICE_GET_CLASS(dev);

    // Setup default properties.
    if (vsock_fwd_dev->guest_port == 0) {
        vsock_fwd_dev->guest_port = 5555;
    } else {
        LOG(WARNING) << "The ADBD guest port is usually 5555, not " << vsock_fwd_dev->guest_port;
    }
    vsock_fwd_dev->on_connect = adb_vsock_connected;

    if (VLOG_IS_ON(1)) {
        vsock_fwd_dev->on_accept = adb_vsock_accept;
    }

    // Initialize the vsock port forwarder.
    adc->vsock_port_fwd_realize(dev, errp);
}

static void adb_vsock_class_init(ObjectClass* oc, void* data) {
    AdbDeviceClass* dc = ADB_VSOCK_DEVICE_CLASS(oc);

    // Re-direct the realize to us, and make sure we can call the parent.
    dc->vsock_port_fwd_realize = dc->parent_class.realize;
    dc->parent_class.realize = adb_vsock_realize;
}

static const TypeInfo adb_vsock_type_info = {
        .name = TYPE_ADB_VSOCK_DEVICE,
        .parent = TYPE_VSOCK_FWD,
        .instance_size = sizeof(AdbVSockDev),
        .class_size = sizeof(AdbDeviceClass),
        .class_init = adb_vsock_class_init,
};

static void register_types(void) {
    type_register_static(&adb_vsock_type_info);
}
type_init(register_types);
