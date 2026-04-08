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

#include "goldfish/avd_info/avd_info.h"

#include <chrono>
#include <fstream>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"

#include "VCpuEventLoop.h"
#include "android/base/goldfish/devices/sensor/sensor_device.h"
#include "android/base/qemu_clock.h"
#include "android/base/system.h"
#include "android/crashreport/crash_reporter.h"
#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/ini_file.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/avd_info/avd_private.h"
#include "goldfish/avd_info/gralloc_impl.h"
#include "goldfish/devices/battery/battery.h"
#include "goldfish/devices/boot/boot_properties_device.h"
#include "goldfish/devices/camera/register_device.h"
#include "goldfish/devices/clipboard/clipboard_device.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/fingerprint/fingerprint_device.h"
#include "goldfish/devices/gps/gps_device.h"
#include "goldfish/devices/guest_status/guest_status_device.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
#include "goldfish/devices/unix_pipe/unix_pipe.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/file/file.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/perf_stat_reporter.h"
#include "goldfish/vsock/clear.h"
#include "host-common/constants.h"

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
#undef listen
#undef shutdown
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::avd_info {

using goldfish::devices::ConnectorRegistry;
using goldfish::devices::PingTopic;
using goldfish::devices::cable::SocketPtr;
using goldfish::devices::camera::GrallocDetailsPtr;

namespace {

struct AvdExtendedUniverse : public AvdUniverse {
    AvdExtendedUniverse(std::unique_ptr<AvdProperties> props) : AvdUniverse(std::move(props)) {}
    ~AvdExtendedUniverse() override {
        if (metrics_ping_timer) {
            metrics_ping_timer->Cancel();
        }
        if (perf_stat_reporter_task) {
            perf_stat_reporter_task->Cancel();
        }
    }

    async::EventLoop& GetQemuEventLoop() override { return *qemu_event_loop; }
    goldfish::metrics::MetricsReporter& GetMetricsReporter() override { return *metrics_reporter; }

    ConnectorRegistry connector_registry;
    ConnectorRegistry test_tools_connector_registry;
    avd_universe::battery::ObservableBattery::ScopedCallbackHandle battery_subscription;
    avd_universe::guest_status::ObservableTimestamp::ScopedCallbackHandle bootcomplete_subscription;
    std::unique_ptr<goldfish::metrics::MetricsReporter> metrics_reporter;
    std::shared_ptr<goldfish::async::EventLoop::Timer> metrics_ping_timer;

    std::unique_ptr<async::EventLoop> qemu_event_loop;
    std::vector<VCpuEventLoop> qemu_cpu_loops;
    std::unique_ptr<goldfish::metrics::PerfStatReporter> perf_stat_reporter;
    std::shared_ptr<goldfish::async::EventLoop::Timer> perf_stat_reporter_task;
};

struct AvdInfoDev {
    DeviceClass parent_class;
    // `mutable_props` is valid only between `instance_init` and `realize`.
    // It moves into `universe` in `realize` and stays there as immutable.
    AvdProperties* mutable_props;
    AvdExtendedUniverse* universe;  // deleted in `avd_info_instance_finalize`
};

#define TYPE_AVD "avdstart"
#define AVD_INFO_DEV(obj) OBJECT_CHECK(AvdInfoDev, (obj), TYPE_AVD)
#define AVD_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdInfoDev, obj, TYPE_AVD)

AvdExtendedUniverse* gGlobalAvdUniverseInstance;  // do not read directly, use `GetAvd` instead

AvdExtendedUniverse& getAvdImpl() {
    CHECK(gGlobalAvdUniverseInstance)
            << "The AvdUniverse instance is not yet available. "
               "This is a QEMU configuration issue which must be fixed in the launcher.";

    return *gGlobalAvdUniverseInstance;
}

}  // namespace

AvdUniverse::AvdUniverse(std::unique_ptr<AvdProperties> props)
        : props_(std::move(props)), sensors_physical_model_(props_->hw_config) {
    guest_status_.reset.SetValue(
            absl::UnixEpoch() +
            absl::Milliseconds(android::base::System::Get()->GetProcessTimes().wall_clock_ms));
}

void AvdUniverse::SetActiveMultiDisplayDevice(
        std::shared_ptr<devices::multidisplay::MultiDisplayDevice> device) {
    absl::MutexLock lock(&device_mutex_);
    active_multi_display_device_ = device;
}

std::shared_ptr<devices::multidisplay::MultiDisplayDevice>
AvdUniverse::GetActiveMultiDisplayDevice() {
    absl::MutexLock lock(&device_mutex_);
    return active_multi_display_device_;
}

AvdUniverse& GetAvd() {
    return getAvdImpl();
}

void UniverseBuildComplete() {
    AvdExtendedUniverse& u = getAvdImpl();
    u.connector_registry.Listen(5000);
    u.test_tools_connector_registry.Listen(5002);
}

namespace {
void BqlSafeUnregisterEmulatorReset(QEMUResetHandler* func, void* opaque) {
    if (bql_locked()) {
        qemu_unregister_reset(func, opaque);
    } else {
        LOG(FATAL) << "Attempted to call qemu_unregister_reset with BQL held.";
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

absl::Status ValidateAvdProps(AvdProperties& avd_props) {
    if (avd_props.serial_number <= 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "serial_number is unspecified (it must be > 0): %d", avd_props.serial_number));
    }

    if (avd_props.adb_port <= 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "adb_port is unspecified (it must be > 0): %d", avd_props.adb_port));
    }

    if (avd_props.metrics_session_id == ::goldfish::metrics::Uuid::Zero() &&
        avd_props.metrics_writer_config.type != goldfish::metrics::MetricsWriterType::kNone) {
        return absl::InvalidArgumentError(
                "metrics_session_id should be non-zero when metrics_writer is set");
    }

    fs::path hw_path = avd_props.avd_content_path / CORE_HARDWARE_INI;
    auto hw_ini = std::make_unique<android::goldfish::IniFile>(hw_path);
    if (!hw_ini->Read()) {
        return absl::NotFoundError(
                absl::StrFormat("Failed to parse hardware ini: %s", hw_path.string()));
    }

    avd_props.hw_config.Load(*hw_ini);
    return absl::OkStatus();
}

void avd_info_realize(DeviceState* dev, Error** errp) {
    VLOG(1) << "avd_info_realize: " << object_get_canonical_path(OBJECT(dev));

    AvdInfoDev* avd_info = AVD_INFO_DEV(dev);
    DCHECK(avd_info);

    VLOG(1) << "Device configuration, AVD name: '" << avd_info->mutable_props->avd_name << "'";

    // Set the system clock to the QEMU implementation.
    android::base::IClock::Set(std::make_unique<android::base::QemuClock>());

    if (auto s = ValidateAvdProps(*avd_info->mutable_props); !s.ok()) {
        error_setg(errp, "%s", s.ToString().c_str());
        return;
    }

    avd_info->universe = new AvdExtendedUniverse(
            std::unique_ptr<AvdProperties>(std::exchange(avd_info->mutable_props, nullptr)));

    auto& avd_universe = *avd_info->universe;
    const AvdProperties& avd_props = avd_universe.Props();

    LOG(INFO) << "Loaded avd directory: " << avd_props.avd_content_path;

    if (!avd_props.snapshot_name.empty()) {
        auto bootstatus_ini = avd_props.avd_content_path / "snapshots" / avd_props.snapshot_name /
                              "bootstatus.ini";
        if (std::filesystem::exists(bootstatus_ini)) {
            avd_universe.GetGuestStatus().bootcomplete.SetValue(absl::Now());
        }

        avd_universe.bootcomplete_subscription = android::base::eventing::MakeScopedCallback(
                avd_universe.GetGuestStatus().bootcomplete, [bootstatus_ini](absl::Time time) {
                    if (time != absl::UnixEpoch()) {
                        std::error_code ec;
                        std::filesystem::create_directories(bootstatus_ini.parent_path(), ec);
                        std::ofstream ofs(bootstatus_ini);
                        if (ofs) {
                            ofs << "bootcomplete=1\n";
                        }
                    }
                });
    }

    auto* client_loop = goldfish::async::globalEventLoop();

    avd_universe.metrics_reporter =
            std::make_unique<::goldfish::metrics::MetricsReporter>(avd_props.metrics_session_id);
    ::goldfish::metrics::ConfigureMetricsWriter(*avd_universe.metrics_reporter,
                                                avd_props.metrics_writer_config, *client_loop);
    // PING every 5 minutes.
    using namespace std::chrono_literals;
    avd_universe.metrics_ping_timer = client_loop->ScheduleRepeating(
            [metrics_reporter = avd_universe.metrics_reporter.get()] {
                metrics_reporter->Report([](android_studio::AndroidStudioEvent& event) {});
            },
            0s, 300s);

    avd_universe.qemu_event_loop = goldfish::async::QemuEventLoop::Create();
    auto& qemu_loop = avd_universe.qemu_event_loop;
    android::crashreport::CrashReporter::GetCrashingHangDetector().AddWatchedLooper(
            "QemuEventLoop", *qemu_loop, absl::Seconds(15));

    avd_universe.qemu_cpu_loops = createVCpuEventLoops();
    std::vector<goldfish::async::EventLoop*> vcpu_loop_ptrs;
    vcpu_loop_ptrs.reserve(avd_universe.qemu_cpu_loops.size());
    for (auto& loop : avd_universe.qemu_cpu_loops) {
        android::crashreport::CrashReporter::GetCrashingHangDetector().AddWatchedLooper(
                absl::StrCat("QemuCpuLoop:", loop.getCpuIndex()), loop, absl::Seconds(15));
        vcpu_loop_ptrs.push_back(&loop);
    }
    avd_universe.perf_stat_reporter = std::make_unique<goldfish::metrics::PerfStatReporter>(
            client_loop, vcpu_loop_ptrs, avd_props.hw_config, avd_props.dump_perf_stat_path);
    // One-shot task to send perf metrics report after 60s.
    avd_universe.perf_stat_reporter_task = client_loop->ScheduleDelayed(
            [&avd_universe] {
                avd_universe.metrics_reporter->Report(
                        [&avd_universe](android_studio::AndroidStudioEvent& event) {
                            VLOG(1) << "Reporting perf stat metric";
                            event.set_kind(
                                    android_studio::AndroidStudioEvent::EMULATOR_PERFORMANCE_STATS);
                            avd_universe.perf_stat_reporter->FillEvent(event);
                        });
            },
            /*initial_delay=*/60s);

    auto* registry = &avd_universe.connector_registry;

    namespace DEVS = ::goldfish::devices;

    DEVS::sensor::ISensorDevice::RegisterDevice(&avd_universe.GetSensorsPhysicalModel(), registry,
                                                avd_props.avd_type, avd_props.avd_api,
                                                avd_props.hw_config, client_loop, qemu_loop.get());
    DEVS::clipboard::IClipboardDevice::RegisterDevice(&avd_universe.GetClipboardChannel(), registry,
                                                      client_loop, qemu_loop.get());
    DEVS::guest_status::IGuestStatusDevice::RegisterDevice(
            &avd_universe.GetGuestStatus(), &avd_universe.GetGrpcNotificationChannel(), registry,
            {qemu_register_reset, BqlSafeUnregisterEmulatorReset}, client_loop, qemu_loop.get(),
            avd_props.quit_after_boot_timeout_seconds);
    DEVS::fingerprint::IFingerprintDevice::RegisterDevice(&avd_universe.GetFingerprintSensor(),
                                                          registry, client_loop, qemu_loop.get());
    DEVS::gps::IGpsDevice::RegisterDevice(&avd_universe.GetLocation(), registry, client_loop,
                                          qemu_loop.get());

    auto multi_display_device =
            std::make_shared<DEVS::multidisplay::MultiDisplayDevice>(client_loop);
    avd_universe.SetActiveMultiDisplayDevice(multi_display_device);
    DEVS::multidisplay::MultiDisplayDevice::RegisterDevice(multi_display_device, registry,
                                                           client_loop, qemu_loop.get());

    std::string emulatedCameraProp;
    DEVS::camera::RegisterDevice(registry, &emulatedCameraProp, avd_props.hw_config,
                                 []() { return GetGrallocImpl(); });

    using namespace std::string_literals;
    if (auto props = devices::boot::IBootPropertiesDevice::MakeProperties({
            {"qemu.sf.fake_camera"s, emulatedCameraProp},
            {"qemu.sf.lcd_density"s, absl::StrCat(avd_props.hw_config.hw_lcd_density)},
            // This is the same value that is passed to the virtio-wifi module.
            {"net.wifi_mac_prefix"s, absl::StrCat(avd_props.serial_number)},
        });
        !props.ok()) {
        LOG(FATAL) << "Failed to parse boot property strings: " << props.status();
    } else {
        DEVS::boot::IBootPropertiesDevice::RegisterDevice(registry, *std::move(props), client_loop,
                                                          qemu_loop.get());
    }

    DEVS::unix_pipe::IUnixPipe::RegisterDevice(&avd_universe.test_tools_connector_registry,
                                               client_loop, qemu_loop.get());

    ::goldfish::display::qemu_multidisplay::ConfigureMultiDisplay(client_loop, qemu_loop.get());

    // Initialize the battery to a default state and register it.
    avd_universe.battery_subscription = DEVS::battery::RegisterBattery(
            &avd_universe.GetBattery(), avd_props.hw_config.hw_battery, qemu_loop.get());

    // Make avd universe visible to other modules.
    gGlobalAvdUniverseInstance = avd_info->universe;
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
    fs::path dir(value);
    if (!android::base::file::is_dir(dir)) {
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

void avd_info_set_snapshot_name(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->snapshot_name = value;
}

void avd_info_set_quit_after_boot_timeout(Object* obj, Visitor* v, const char* name, void* opaque,
                                          Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->quit_after_boot_timeout_seconds = value;
}

void avd_info_set_metrics_session(Object* obj, const char* value, Error** errp) {
    if (auto s = ::goldfish::metrics::Uuid::FromString(value); !s.ok()) {
        error_setg(errp, "metrics_session - failed to parse UUID: %s - %s",
                   s.status().ToString().c_str(), value);
        return;
    } else {
        AVD_INFO_DEV(obj)->mutable_props->metrics_session_id = *std::move(s);
    }
}

void avd_info_set_metrics_writer(Object* obj, Visitor* v, const char* name, void* opaque,
                                 Error** errp) {
    int32_t value;
    if (!visit_type_int32(v, name, &value, errp)) {
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->metrics_writer_config.type =
            static_cast<goldfish::metrics::MetricsWriterType>(value);
}

void avd_info_set_metrics_file_path(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->metrics_writer_config.file_path = value;
}

void avd_info_set_metrics_spool_dir(Object* obj, const char* value, Error** errp) {
    AVD_INFO_DEV(obj)->mutable_props->metrics_writer_config.studio_spool_dir = value;
}

void avd_info_set_dump_perf_stat_path(Object* obj, const char* value, Error** errp) {
    fs::path path(value);
    if (!android::base::file::is_dir(path.parent_path())) {
        error_setg(errp, "dump_perf_stat_path parent is not a valid directory: %s", value);
        return;
    }

    AVD_INFO_DEV(obj)->mutable_props->dump_perf_stat_path = path;
}

void avd_info_unrealize(DeviceState* dev) {
    VLOG(1) << "avd_info_unrealize";
    gGlobalAvdUniverseInstance = nullptr;
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
    object_class_property_add_str(oc, "snapshot_name", nullptr, avd_info_set_snapshot_name);

    object_class_property_add(oc, "quit_after_boot_timeout", "int", nullptr,
                              avd_info_set_quit_after_boot_timeout, nullptr, nullptr);

    object_class_property_add_str(oc, "metrics_session", nullptr, avd_info_set_metrics_session);
    object_class_property_add(oc, "metrics_writer", "int", nullptr, avd_info_set_metrics_writer,
                              nullptr, nullptr);
    object_class_property_add_str(oc, "metrics_file_path", nullptr, avd_info_set_metrics_file_path);
    object_class_property_add_str(oc, "metrics_spool_dir", nullptr, avd_info_set_metrics_spool_dir);
    object_class_property_add_str(oc, "dump_perf_stat_path", nullptr,
                                  avd_info_set_dump_perf_stat_path);

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
    if (avd_info->universe->qemu_event_loop) {
        auto f = avd_info->universe->qemu_event_loop->Shutdown();
        // In the current Qemu implementation, we are already running on the Qemu main thread and so
        // shutdown will have run serially.
        if (f.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            LOG(FATAL) << "Qemu loop shutdown failed to complete within 15s";
        }
        auto s = f.get();
        LOG_IF(FATAL, !s.ok()) << "Qemu loop shutdown failed: " << s;
        avd_info->universe->qemu_event_loop.reset();
    }
    goldfish::vsock::clear();
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
