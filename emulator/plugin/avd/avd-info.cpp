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
#include "absl/strings/str_cat.h"

#include "aemu/base/files/IniFile.h"
#include "android/base/system/qemu_clock.h"
#include "android/boot/BootPropertiesDevice.h"
#include "android/camera/registerDevice.h"
#include "android/clipboard/ClipboardDevice.h"
#include "android/fingerprint/FingerprintDevice.h"
#include "android/goldfish/config/hardware_config.h"
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
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "system/reset.h"
}
// IWYU pragma: end_keep
// clang-format on

#define CORE_HARDWARE_INI "hardware-qemu.ini"

using goldfish::avd_info::getGrallocImpl;
using goldfish::devices::PingTopic;
using goldfish::devices::cable::SocketPtr;
using goldfish::devices::camera::GrallocDetailsPtr;

namespace goldfish::avd_info {

namespace {
using devices::ConnectorRegistry;

std::unique_ptr<goldfish::avd_info::AvdProperties> gAvd;
} // namespace

const AvdProperties *get_avd() {
    if (gAvd) {
        return gAvd.get();
    }
    return nullptr;
}

ConnectorRegistry& connector_registry() {
    return ConnectorRegistry::defaultRegistry();
}

namespace {
void DummyRegisterEmulatorReset(QEMUResetHandler* func, void* opaque) {}

std::unique_ptr<async::EventLoop> gQemuLoop;

void avd_info_realize(DeviceState* dev, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(dev);

    // Set the system clock to the QEMU implementation.
    android::base::IClock::set(std::make_unique<android::base::QemuClock>());

    if (avd_info->serial_number <= 0) {
        error_setg(errp, "serial_number is unspecified (it must be > 0): %d", avd_info->serial_number);
        return;
    }
    if (avd_info->adb_port <= 0) {
        error_setg(errp, "adb_port is unspecified (it must be > 0): %d", avd_info->adb_port);
        return;
    }

    VLOG(1) << "Device configuration, avd_info: " << *avd_info;
    LOG(INFO) << "Loaded avd: " << avd_info->avd_content_path;
    gAvd = std::make_unique<goldfish::avd_info::AvdProperties>();
    gAvd->avd_info = avd_info;

    auto hw_path = avd_info->avd_content_path / CORE_HARDWARE_INI;
    auto hw_ini = std::make_unique<android::goldfish::IniFile>(hw_path);
    if (!hw_ini->read()) {
        error_setg(errp, "adb_port is unspecified (it must be > 0): %d", avd_info->adb_port);
        // TODO
    }
    gAvd->hw_config.load(hw_ini.get());

    auto *clientLoop = goldfish::async::globalEventLoop();
    gQemuLoop = goldfish::async::QemuEventLoop::create();

    auto *registry = &connector_registry();

    goldfish::devices::sensor::ISensorDevice::registerDevice(registry, gAvd->hw_config, clientLoop,
                                                             gQemuLoop.get());
    goldfish::devices::clipboard::IClipboardDevice::registerDevice(registry, clientLoop,
                                                                   gQemuLoop.get());
    goldfish::devices::guest_status::IGuestStatusDevice::registerDevice(
            registry, qemu_register_reset, clientLoop, gQemuLoop.get(), avd_info->quit_after_boot_timeout_seconds);
    goldfish::devices::fingerprint::IFingerprintDevice::registerDevice(registry, clientLoop,
                                                                       gQemuLoop.get());
    goldfish::devices::gps::IGpsDevice::registerDevice(registry, clientLoop, gQemuLoop.get());

    std::string emulatedCameraProp;
    goldfish::devices::camera::registerDevice(registry, &emulatedCameraProp, gAvd->hw_config, []() { return getGrallocImpl(); });

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

void avd_info_set_serial_number(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }
    avd_info->serial_number = value;
}

void avd_info_set_adb_port(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }
    avd_info->adb_port = value;
}

void avd_info_set_avd_name(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->avd_name = value;
}

void avd_info_set_avd_id(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->avd_id = value;
}

void avd_info_set_avd_abi(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->avd_abi = value;
}

void avd_info_set_avd_api(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }
    avd_info->avd_api = value;
}

void avd_info_set_avd_dir(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    std::filesystem::path dir(value);
    if (!std::filesystem::is_directory(dir)) {
        error_setg(errp, "avd_dir specified is not a valid directory: %s", value);
        return;
    }
    avd_info->avd_content_path = dir;
}

void avd_info_set_build_sdk(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->build_sdk = value;
}

void avd_info_set_build_id(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->build_id = value;
}

void avd_info_set_build_flavour(Object* obj, const char* value, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    avd_info->build_flavour = value;
}

void avd_info_set_quit_after_boot_timeout(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    int32_t value;

    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    avd_info->quit_after_boot_timeout_seconds = value;
}

void avd_info_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "serial_number", "int", nullptr, avd_info_set_serial_number, nullptr, nullptr);
    object_class_property_add(oc, "adb_port", "int", nullptr, avd_info_set_adb_port, nullptr, nullptr);

    object_class_property_add_str(oc, "avd_name", nullptr, avd_info_set_avd_name);
    object_class_property_add_str(oc, "avd_id", nullptr, avd_info_set_avd_id);
    object_class_property_add_str(oc, "avd_abi", nullptr, avd_info_set_avd_abi);
    object_class_property_add(oc, "avd_api", "int", nullptr, avd_info_set_avd_api, nullptr, nullptr);
    object_class_property_add_str(oc, "avd_dir", nullptr, avd_info_set_avd_dir);

    object_class_property_add_str(oc, "build_sdk", nullptr, avd_info_set_build_sdk);
    object_class_property_add_str(oc, "build_id", nullptr, avd_info_set_build_id);
    object_class_property_add_str(oc, "build_flavour", nullptr, avd_info_set_build_flavour);

    object_class_property_add(oc, "quit_after_boot_timeout", "int", nullptr, avd_info_set_quit_after_boot_timeout, nullptr, nullptr);

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
