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

#include "goldfish/adb/adb-device.h"

#include <memory>

#include "absl/log/log.h"

#include "android/emulation/control/adb/AdbHostServer.h"
#include "android/emulation/control/adb/AdbMessageLogger.h"

#include "goldfish/avd/avd-info.h"
#include "goldfish/device_registry/DeviceRegistry.h"
// clang-format off
// IWYU pragma: begin_keep
#include "goldfish/vsock/vsock_port_fwd.h"

extern "C" {
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
}
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::adb_device {

// Define a subclass for the VSockFwdDevice, where we are going
// to override the realize method, in the realize method we will
// override the common properties
struct AdbVSockDev {
    VSockFwdDev parent;
    bool monitor;
};

struct AdbDeviceClass {
    DeviceClass parent_class;
    DeviceRealize vsock_port_fwd_realize;
};

#define TYPE_ADB_VSOCK_DEVICE "virtio-goldfish-adb"
OBJECT_DECLARE_TYPE(AdbVSockDev, AdbDeviceClass, ADB_VSOCK_DEVICE)
#define ADB_VSOCK_DEV(obj) OBJECT_CHECK(AdbVSockDev, (obj), TYPE_ADB_VSOCK_DEVICE)
#define ADB_VSOCK_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AdbDeviceClass, obj, TYPE_ADB_VSOCK_DEVICE)

using goldfish::adb::AdbHostServer;
using goldfish::adb::AdbLogger;

namespace {

// QEMU device configuration logic
void adb_vsock_connected(VSockFwdDev* device) {
    auto adb_server = AdbHostServer::getClientPort();
    LOG(WARNING) << "Notifying adb server on port " << adb_server
              << " that adbd is available on localhost:" << device->host_port;
    AdbHostServer::notify(device->host_port, adb_server);

    auto *avd = goldfish::avd_info::get_avd();
    // Make it easier for tests to find us.
    // Note that this format is implemented in adb here:
    // https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:packages/modules/adb/client/transport_emulator.cpp;l=79;drc=6d17979f120fcba950b024d1cc62ae24ab600a71
    int expected_serial = device->host_port - 1;
    if (avd->serial_number != expected_serial) {
        LOG(WARNING) << "Actual and expected serial numbers differ: " << avd->serial_number << " != " << expected_serial;
    }
}

void adb_vsock_realize(DeviceState* dev, Error** errp) {
    VSockFwdDev* vsock_fwd_dev = VSOCK_FWD_DEV(dev);
    auto adc = ADB_VSOCK_DEVICE_GET_CLASS(dev);
    AdbVSockDev* adb = ADB_VSOCK_DEV(dev);

    // Setup default properties.
    if (vsock_fwd_dev->guest_port == 0) {
        vsock_fwd_dev->guest_port = 5555;
    } else {
        LOG(WARNING) << "The ADBD guest port is usually 5555, not " << vsock_fwd_dev->guest_port;
    }
    vsock_fwd_dev->on_connect = adb_vsock_connected;

    if (adb->monitor) {
        VLOG(1) << "ADB monitor enabled";
        vsock_fwd_dev->data_sniffer_factory = [host = vsock_fwd_dev->host_port,
                                               guest = vsock_fwd_dev->guest_port] {
            return std::make_unique<AdbLogger>(host, guest);
        };
    }

    // Initialize the vsock port forwarder.
    adc->vsock_port_fwd_realize(dev, errp);
    DeviceRegistry::get().setOnce(properties::kAdbPort, vsock_fwd_dev->host_port);
}

void adb_vsock_set_monitor(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AdbVSockDev* adb = ADB_VSOCK_DEV(obj);

    bool monitor;
    if (!visit_type_bool(v, name, &monitor, errp)) {
        error_setg(errp, "failed to parse monitor bool");
        return;
    }

    adb->monitor = monitor;
}

void adb_vsock_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "monitor", "bool", nullptr,
                              adb_vsock_set_monitor, nullptr, nullptr);

    AdbDeviceClass* dc = ADB_VSOCK_DEVICE_CLASS(oc);

    // Re-direct the realize to us, and make sure we can call the parent.
    dc->vsock_port_fwd_realize = dc->parent_class.realize;
    dc->parent_class.realize = adb_vsock_realize;
}

const TypeInfo adb_vsock_type_info = {
    .name = TYPE_ADB_VSOCK_DEVICE,
    .parent = TYPE_VSOCK_FWD,
    .instance_size = sizeof(AdbVSockDev),
    .class_size = sizeof(AdbDeviceClass),
    .class_init = adb_vsock_class_init,
};
}  // namespace

void adb_device_register_types(void) {
    type_register_static(&adb_vsock_type_info);
}

}  // namespace goldfish::adb_device
