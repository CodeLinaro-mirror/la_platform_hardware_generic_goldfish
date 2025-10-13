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

#include "goldfish/avd/avd-info.h"

#include <memory>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "android/base/system/qemu_clock.h"
#include "android/boot/BootPropertiesDevice.h"
#include "android/camera/registerDevice.h"
#include "android/clipboard/ClipboardDevice.h"
#include "android/fingerprint/FingerprintDevice.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/display/MultiDisplay.h"
#include "android/gps/GpsDevice.h"
#include "android/misc/GuestStatusDevice.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/avd/GrallocImpl.h"
#include "goldfish/avd/global-event-loop.h"
#include "goldfish/devices/sensor/SensorDevice.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "system/reset.h"
}
// IWYU pragma: end_keep
// clang-format on

using android::goldfish::Avd;
using goldfish::avd_info::getGrallocImpl;
using goldfish::devices::PingTopic;
using goldfish::devices::cable::SocketPtr;
using goldfish::devices::camera::GrallocDetailsPtr;

static std::unique_ptr<Avd> gAvd;

namespace goldfish::avd_info {
using devices::ConnectorRegistry;

android::goldfish::Avd* get_avd() {
    if (gAvd) {
        return gAvd.get();
    }
    return nullptr;
}

ConnectorRegistry& deviceRegistry() {
    return ConnectorRegistry::defaultRegistry();
}

namespace {
void DummyRegisterEmulatorReset(QEMUResetHandler* func, void* opaque) {}

std::unique_ptr<async::EventLoop> gQemuLoop;

void avd_info_realize(DeviceState* dev, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(dev);

    // Set the system clock to the QEMU implementation.
    android::base::IClock::set(std::make_unique<android::base::QemuClock>());

    auto avd_status = android::goldfish::FileBackedAvd::parse(avd_info->ini_path,
                                                              /*sysdir_override=*/std::string());
    if (!avd_status.ok()) {
        LOG(FATAL) << "Unable to load: " << avd_info->ini_path
                   << " due to: " << avd_status.status().message();
        return;
    }

    VLOG(1) << "Device configuration, avd_info: " << *avd_info;
    LOG(INFO) << "Loaded avd:" << avd_info->ini_path;
    gAvd = std::move(avd_status.value());

    auto clientLoop = goldfish::async::globalEventLoop();
    gQemuLoop = goldfish::async::QemuEventLoop::create();

    auto avd = gAvd.get();
    auto registry = &goldfish::avd_info::deviceRegistry();

    goldfish::devices::sensor::ISensorDevice::registerDevice(registry, *avd, clientLoop,
                                                             gQemuLoop.get());
    goldfish::devices::clipboard::IClipboardDevice::registerDevice(registry, avd, clientLoop,
                                                                   gQemuLoop.get());
    goldfish::devices::guest_status::IGuestStatusDevice::registerDevice(
            registry, qemu_register_reset, clientLoop, gQemuLoop.get());
    goldfish::devices::fingerprint::IFingerprintDevice::registerDevice(registry, clientLoop,
                                                                       gQemuLoop.get());
    goldfish::devices::gps::IGpsDevice::registerDevice(registry, clientLoop, gQemuLoop.get());

    std::string emulatedCameraProp;
    goldfish::devices::camera::registerDevice(registry, &emulatedCameraProp, *avd,
                                              []() { return getGrallocImpl(); });

    using namespace std::string_literals;
    goldfish::devices::boot::IBootPropertiesDevice::registerDevice(
            registry,
            {
                {"qemu.sf.fake_camera"s, emulatedCameraProp},
                {"qemu.sf.lcd_density"s, "420"s},
                // This is the same value that is passed to the virtio-wifi module.
                {"net.wifi_mac_prefix"s, absl::StrCat(avd_info->serial_number)},
                // TODO(b/450338546): hack hack hack
                // These properties should be added automatically by http://ac/device/generic/goldfish/qemu-props/vport_parser.cpp
                // But it isn't currently working so we hack them in here.
                // They will be incorrect if the order of serial port creation changes.
#if defined(__APPLE__)
                {"vendor.qemu.vport.uwb"s, "/dev/vport6p2"s},
                {"vendor.qemu.vport.bluetooth"s, "/dev/vport6p3"s},
#else
                {"vendor.qemu.vport.uwb"s, "/dev/vport8p2"s},
                {"vendor.qemu.vport.bluetooth"s, "/dev/vport8p3"s},
#endif
            },
            &DummyRegisterEmulatorReset, clientLoop, gQemuLoop.get());

    android::goldfish::QemuMultidisplay::configureMultiDisplay(clientLoop, gQemuLoop.get());
}

void avd_info_set_ini_path(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->ini_path = value;
}

void avd_info_set_serial_number(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    int32_t value;

    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    avd_info->serial_number = value;
}

void avd_info_class_init(ObjectClass* oc, void* data) {
    object_class_property_add_str(oc, "ini_path", NULL, avd_info_set_ini_path);
    object_class_property_set_description(oc, "ini_path",
                                          "the path to the AVD's configuration (.ini) file.");

    object_class_property_add(oc, "serial_number", "int", nullptr, avd_info_set_serial_number, NULL, NULL);
    object_class_property_set_description(oc, "serial_number", "The serial number of this emulator");

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = avd_info_realize;
}

const TypeInfo avd_info_type_info = {
    .name = TYPE_AVD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AvdInfoDev),
    .class_init = avd_info_class_init,
};

}  // namespace

void avd_info_register_types(void) {
    type_register_static(&avd_info_type_info);
}

}  // namespace goldfish::avd_info
