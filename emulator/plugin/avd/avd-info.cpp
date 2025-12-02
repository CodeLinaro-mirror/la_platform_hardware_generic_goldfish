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

#include <chrono>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "aemu/base/files/IniFile.h"

#include "android/base/system/qemu_clock.h"
#include "android/boot/BootPropertiesDevice.h"
#include "android/camera/registerDevice.h"
#include "android/clipboard/ClipboardDevice.h"
#include "android/crashreport/CrashReporter.h"
#include "android/fingerprint/FingerprintDevice.h"
#include "android/goldfish/config/device_type.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/gps/GpsDevice.h"
#include "android/misc/GuestStatusDevice.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/avd/GrallocImpl.h"
#include "goldfish/avd/global-event-loop.h"
#include "goldfish/devices/sensor/SensorDevice.h"
#include "goldfish/display/MultiDisplay.h"

#include "VCpuEventLoop.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "system/reset.h"
#include "qemu/main-loop.h"
}
#undef shutdown
// IWYU pragma: end_keep
// clang-format on

#define CORE_HARDWARE_INI "hardware-qemu.ini"

using goldfish::avd_info::getGrallocImpl;
using goldfish::devices::PingTopic;
using goldfish::devices::cable::SocketPtr;
using goldfish::devices::camera::GrallocDetailsPtr;

namespace goldfish::avd_info {
namespace {

struct AvdInfoDev {
    DeviceClass parent_class;
    // `mutable_props` is valid only between `instance_init` and `realize`.
    // It moves into `universe` in `realize` and stays there as immutable.
    AvdProperties* mutable_props;
    AvdUniverse* universe;
};

#define TYPE_AVD "avdstart"
#define AVD_INFO_DEV(obj) OBJECT_CHECK(AvdInfoDev, (obj), TYPE_AVD)
#define AVD_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdInfoDev, obj, TYPE_AVD)

using devices::ConnectorRegistry;

AvdUniverse* gAvdUniverse;

std::unique_ptr<async::EventLoop> gQemuLoop;
std::vector<VCpuEventLoop> gQemuCpuLoops;

}  // namespace

AvdUniverse::AvdUniverse(std::unique_ptr<AvdProperties> props) : mProps(std::move(props)) {}

AvdUniverse& getAvd() {
    if (!gAvdUniverse) {
        LOG(FATAL) << "The AvdUniverse instance is not yet available. "
                      "This is a QEMU configuration issue which must be fixed in the launcher.";
    }

    return *gAvdUniverse;
}

ConnectorRegistry& connector_registry() {
    return ConnectorRegistry::defaultRegistry();
}

::goldfish::async::EventLoop *getQemuEventLoop() {
    if (!gQemuLoop) {
        LOG(FATAL) << "The QemuEventLoop instance is not yet available. "
                      "This is a QEMU configuration issue which must be fixed in the launcher.";
    }

    return gQemuLoop.get();
}

namespace {
void DummyRegisterEmulatorReset(QEMUResetHandler* func, void* opaque) {}

void BqlSafeUnregisterEmulatorReset(QEMUResetHandler* func, void* opaque) {
    if (bql_locked()) {
        qemu_unregister_reset(func, opaque);
    } else {
        abort();
    }
}

std::vector<VCpuEventLoop> createVCpuEventLoops() {
    int cpus_count = VCpuEventLoop::cpus_count();
    std::vector<VCpuEventLoop> loops;
    loops.reserve(cpus_count);
    for (int i = 0; i < cpus_count; ++i) {
        loops.emplace_back(i);
    }
    return loops;
}

void avd_info_realize(DeviceState* dev, Error** errp) {
    VLOG(1) << "avd_info_realize: " << object_get_canonical_path(OBJECT(dev));

    AvdInfoDev* avd_info = AVD_INFO_DEV(dev);
    assert(avd_info);

    std::unique_ptr<AvdProperties> mut_avd_props(std::exchange(avd_info->mutable_props, nullptr));

    // Set the system clock to the QEMU implementation.
    android::base::IClock::set(std::make_unique<android::base::QemuClock>());

    if (mut_avd_props->serial_number <= 0) {
        error_setg(errp, "serial_number is unspecified (it must be > 0): %d",
                   mut_avd_props->serial_number);
        return;
    }
    if (mut_avd_props->adb_port <= 0) {
        error_setg(errp, "adb_port is unspecified (it must be > 0): %d", mut_avd_props->adb_port);
        return;
    }

    VLOG(1) << "Device configuration, AVD name: '" << mut_avd_props->avd_name << "'";

    std::filesystem::path hw_path = mut_avd_props->avd_content_path / CORE_HARDWARE_INI;
    auto hw_ini = std::make_unique<android::goldfish::IniFile>(hw_path);
    if (!hw_ini->read()) {
        error_setg(errp, "Failed to parse hardware ini: %s", hw_path.string().c_str());
        return;
    }
    mut_avd_props->hw_config.load(*hw_ini);

    avd_info->universe = new AvdUniverse(std::move(mut_avd_props));
    gAvdUniverse = avd_info->universe;
    const AvdProperties& avd_props = gAvdUniverse->props();

    LOG(INFO) << "Loaded avd directory: " << avd_props.avd_content_path;

    auto* clientLoop = goldfish::async::globalEventLoop();

    gQemuLoop = goldfish::async::QemuEventLoop::create();
    android::crashreport::CrashReporter::get()->hangDetector().addWatchedLooper("QemuEventLoop", *gQemuLoop, absl::Seconds(15));

    gQemuCpuLoops = createVCpuEventLoops();
    for (auto &loop: gQemuCpuLoops) {
        android::crashreport::CrashReporter::get()->hangDetector().addWatchedLooper(absl::StrCat("QemuCpuLoop:", loop.getCpuIndex()), loop, absl::Seconds(15));
    }

    auto* registry = &connector_registry();

    namespace DEVS = goldfish::devices;

    DEVS::sensor::ISensorDevice::registerDevice(
            registry, avd_props.avd_type, avd_props.avd_api,
            avd_props.hw_config, clientLoop, gQemuLoop.get());
    DEVS::clipboard::IClipboardDevice::registerDevice(registry, clientLoop,
                                                                   gQemuLoop.get());
    DEVS::guest_status::IGuestStatusDevice::registerDevice(
            registry, {qemu_register_reset, BqlSafeUnregisterEmulatorReset}, clientLoop,
            gQemuLoop.get(), avd_props.quit_after_boot_timeout_seconds);
    DEVS::fingerprint::IFingerprintDevice::registerDevice(registry, clientLoop, gQemuLoop.get());
    DEVS::gps::IGpsDevice::registerDevice(registry, clientLoop, gQemuLoop.get());

    std::string emulatedCameraProp;
    DEVS::camera::registerDevice(registry, &emulatedCameraProp, avd_props.hw_config,
                                 []() { return getGrallocImpl(); });

    using namespace std::string_literals;
    DEVS::boot::IBootPropertiesDevice::registerDevice(
            registry,
            {
                {"qemu.sf.fake_camera"s, emulatedCameraProp},
                {"qemu.sf.lcd_density"s, "420"s},
                // This is the same value that is passed to the virtio-wifi module.
                {"net.wifi_mac_prefix"s, absl::StrCat(avd_props.serial_number)},
            },
            clientLoop, gQemuLoop.get());

    ::goldfish::display::QemuMultidisplay::configureMultiDisplay(clientLoop, gQemuLoop.get());
}

void avd_info_set_serial_number(Object* obj, Visitor* v, const char* name, void* opaque,
                                Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->serial_number = value;
}

void avd_info_set_adb_port(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->adb_port = value;
}

void avd_info_set_avd_name(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->avd_name = value;
}

void avd_info_set_avd_id(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->avd_id = value;
}

void avd_info_set_avd_abi(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->avd_abi = value;
}

void avd_info_set_avd_api(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->avd_api = value;
}

void avd_info_set_avd_type(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->avd_type = static_cast<android::goldfish::DeviceType>(value);
}

void avd_info_set_avd_dir(Object* obj, const char* value, Error** errp) {
    std::filesystem::path dir(value);
    if (!std::filesystem::is_directory(dir)) {
        error_setg(errp, "avd_dir specified is not a valid directory: %s", value);
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->avd_content_path = dir;
}

void avd_info_set_build_sdk(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->build_sdk = value;
}

void avd_info_set_build_id(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->build_id = value;
}

void avd_info_set_build_flavour(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->build_flavour = value;
}

void avd_info_set_quit_after_boot_timeout(Object* obj, Visitor* v, const char* name, void* opaque,
                                          Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->quit_after_boot_timeout_seconds = value;
}

void avd_info_unrealize(DeviceState* dev) {
    VLOG(1) << "avd_info_unrealize";
    gAvdUniverse = nullptr;
}

void avd_info_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "serial_number", "int", nullptr, avd_info_set_serial_number,
                              nullptr, nullptr);
    object_class_property_add(oc, "adb_port", "int", nullptr, avd_info_set_adb_port, nullptr,
                              nullptr);

    object_class_property_add_str(oc, "avd_name", nullptr, avd_info_set_avd_name);
    object_class_property_add_str(oc, "avd_id", nullptr, avd_info_set_avd_id);
    object_class_property_add_str(oc, "avd_abi", nullptr, avd_info_set_avd_abi);
    object_class_property_add(oc, "avd_api", "int", nullptr, avd_info_set_avd_api, nullptr,
                              nullptr);
    object_class_property_add(oc, "avd_type", "int", nullptr, avd_info_set_avd_type, nullptr,
                              nullptr);
    object_class_property_add_str(oc, "avd_dir", nullptr, avd_info_set_avd_dir);

    object_class_property_add_str(oc, "build_sdk", nullptr, avd_info_set_build_sdk);
    object_class_property_add_str(oc, "build_id", nullptr, avd_info_set_build_id);
    object_class_property_add_str(oc, "build_flavour", nullptr, avd_info_set_build_flavour);

    object_class_property_add(oc, "quit_after_boot_timeout", "int", nullptr,
                              avd_info_set_quit_after_boot_timeout, nullptr, nullptr);

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = avd_info_realize;
    dc->unrealize = avd_info_unrealize;
}

void avd_info_instance_init(Object* obj) {
    AVD_INFO_DEV(obj)->mutable_props = new AvdProperties();
    add_deletable_object(obj);
}

void avd_info_instance_finalize(Object* obj) {
    VLOG(1) << "avd_info_instance_finalize";
    AvdInfoDev* avd_info = AVD_INFO_DEV(obj);
    auto f = gQemuLoop->shutdown();
    // In the current Qemu implementation, we are already running on the Qemu main thread and so shutdown will have run serially.
    if (f.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        LOG(FATAL) << "Qemu loop shutdown failed to complete within 15s";
    }
    auto s = f.get();
    LOG_IF(FATAL, !s.ok()) << "Qemu loop shutdown failed: " << s;
    gQemuLoop.reset();
    delete avd_info->universe;
    delete avd_info->mutable_props;
}

const TypeInfo avd_info_type_info = {
    .name = TYPE_AVD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AvdInfoDev),
    .instance_init = avd_info_instance_init,
    .instance_finalize = avd_info_instance_finalize,
    .class_init = avd_info_class_init,
};

}  // namespace

void avd_info_register_types(void) {
    type_register_static(&avd_info_type_info);
}

}  // namespace goldfish::avd_info
