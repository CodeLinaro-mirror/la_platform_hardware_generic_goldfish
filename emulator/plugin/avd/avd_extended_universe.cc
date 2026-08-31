// Copyright 2026 The Android Open Source Project
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

#include "avd_extended_universe.h"

#include "android/base/system.h"
#include "android/crashreport/crash_reporter.h"
#include "android/goldfish/vm_interface.h"
#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/avd_info/gralloc_impl.h"
#include "goldfish/devices/vehicle/vehicle_device.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/tools/aemu_version.h"
#include "goldfish/vsock/clear.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/core/qdev.h"
#include "system/reset.h"
#include "qemu/main-loop.h"
}
#undef listen
#undef shutdown
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::avd_info {

using devices::boot::EventLoop;
using namespace goldfish::archive;

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

std::string GetCurrentVkIcd() {
    std::string vk_icd = android::base::System::Get()->GetEnvironmentVariable("ANDROID_EMU_VK_ICD");
    return vk_icd;
}

// note: when the avd is moved to different location,
// the path (such as disk_systemPartition_initPath, or disk_systemPartition_path)
// will change, and that is typical use case
// and should not invalidate snapshot; firstboot properties
// are deprecated and not a factor to consider either.
// in addition, the avd home or sdk root are not valid reason
// to invalidet snapshot. we will likely add more factors to
// ignore in the future. after the exclude, most of the properties
// are important such as all the ones start with hw_, or size related
bool IsExcludedProp(std::string_view name) {
    return name.find("path") != std::string_view::npos ||
           name.find("Path") != std::string_view::npos || name.find("firstboot") == 0 ||
           name == "android_sdk_root" || name == "android_sdk_home" || name == "android_avd_home";
}

}  // namespace

AvdExtendedUniverse::AvdExtendedUniverse(std::unique_ptr<AvdProperties> props)
        : AvdUniverse(std::move(props)) {
    auto& avd_universe = *this;
    const AvdProperties& avd_props = avd_universe.Props();

    LOG(INFO) << "Loaded avd directory: " << avd_props.avd_content_path;
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
                return true;
            },
            0s, 300s);
    avd_universe.GetGuestStatus().SetMetricsReporter(avd_universe.metrics_reporter.get());

    auto is_active = []() {
        auto* vm = android::goldfish::VmOperations::qemuVmOperations();
        return vm && vm->isRunning();
    };

    avd_universe.qemu_event_loop = goldfish::async::QemuEventLoop::Create();
    auto& qemu_loop = avd_universe.qemu_event_loop;
    android::crashreport::CrashReporter::GetCrashingHangDetector().AddWatchedLooper(
            "QemuEventLoop", *qemu_loop, absl::Seconds(15), is_active);

    avd_universe.qemu_cpu_loops = createVCpuEventLoops();
    std::vector<goldfish::async::EventLoop*> vcpu_loop_ptrs;
    vcpu_loop_ptrs.reserve(avd_universe.qemu_cpu_loops.size());
    for (auto& loop : avd_universe.qemu_cpu_loops) {
        android::crashreport::CrashReporter::GetCrashingHangDetector().AddWatchedLooper(
                absl::StrCat("QemuCpuLoop:", loop.getCpuIndex()), loop, absl::Seconds(15),
                is_active);
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

    avd_universe.GetGuestStatus().SetGrpcNotificationChannel(
            &avd_universe.GetGrpcNotificationChannel());

    DEVS::sensor::ISensorDevice::RegisterDevice(&avd_universe.GetSensorsPhysicalModel(), registry,
                                                avd_props.avd_type, avd_props.avd_api,
                                                avd_props.hw_config, client_loop, qemu_loop.get());
    DEVS::clipboard::IClipboardDevice::RegisterDevice(&avd_universe.GetClipboardChannel(), registry,
                                                      client_loop, qemu_loop.get());
    DEVS::vehicle::IVehicleDevice::RegisterDevice(&avd_universe.GetVehicleChannel(), registry,
                                                  client_loop, qemu_loop.get());
    DEVS::guest_status::IGuestStatusDevice::RegisterDevice(
            &avd_universe.GetGuestStatus(), registry,
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

    avd_universe.multi_display = display::IMultiDisplay::Create(client_loop, qemu_loop.get());

    // Initialize the battery to a default state and register it.
    avd_universe.battery_subscription = DEVS::battery::RegisterBattery(
            &avd_universe.GetBattery(), avd_props.hw_config.hw_battery, qemu_loop.get());
}

AvdExtendedUniverse::~AvdExtendedUniverse() {
    if (metrics_ping_timer) {
        metrics_ping_timer->Cancel();
    }
    if (perf_stat_reporter_task) {
        perf_stat_reporter_task->Cancel();
    }

    goldfish::vsock::clear();

    {
        auto& hd = android::crashreport::CrashReporter::GetCrashingHangDetector();

        for (auto& loop : qemu_cpu_loops) {
            hd.RemoveWatchedLooper(loop);
        }

        hd.RemoveWatchedLooper(*qemu_event_loop);
    }

    WaitUntilEventLoopsIdle();
    ShutdownQemuLoop();
}

void AvdExtendedUniverse::WaitUntilEventLoopsIdle() {
    const auto wait_event_loops_idle = [](size_t n, size_t* c, EventLoop** l) -> bool {
        bool updated = false;
        for (; n > 0; --n, ++c, ++l) {
            const size_t nc = (*l)->WaitUntilIdle();
            if (nc != *c) {
                *c = nc;
                updated = true;
            }
        }
        return updated;
    };

    EventLoop* loops[] = {goldfish::async::globalEventLoop(), qemu_event_loop.get()};
    size_t counters[std::size(loops)];
    std::transform(std::begin(loops), std::end(loops), std::begin(counters), [](EventLoop* l) {
        l->ShutdownTimers();
        return l->WaitUntilIdle();
    });

    while (wait_event_loops_idle(std::size(loops), counters, loops)) {
    }
}

void AvdExtendedUniverse::ShutdownQemuLoop() {
    auto f = qemu_event_loop->Shutdown();
    // In the current Qemu implementation, we are already running on the Qemu main thread and so
    // shutdown will have run serially.
    if (f.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        LOG(FATAL) << "Qemu loop shutdown failed to complete within 15s";
    }
    auto s = f.get();
    LOG_IF(FATAL, !s.ok()) << "Qemu loop shutdown failed: " << s;
}

async::EventLoop& AvdExtendedUniverse::GetQemuEventLoop() {
    return *qemu_event_loop;
}

goldfish::metrics::MetricsReporter& AvdExtendedUniverse::GetMetricsReporter() {
    return *metrics_reporter;
}

display::IMultiDisplay& AvdExtendedUniverse::GetMultiDisplay() const {
    return *multi_display;
}

absl::Status AvdExtendedUniverse::OnSave(archive::IWriter& writer) const {
    OnSaveProps(writer);
    OnSavePhysicalState(writer);

    if (auto s = GetMultiDisplay().Save(writer); !s.ok()) {
        LOG(WARNING) << "Failed to save multidisplay state: " << s;
        return absl::UnknownError("-1");
    }

    return absl::OkStatus();
}

void AvdExtendedUniverse::OnSaveProps(archive::IWriter& writer) const {
    const auto& p = Props();
    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    std::string vk_icd = GetCurrentVkIcd();
    LOG(INFO) << "Saving AvdProperties: "
              << "avd_abi=" << p.avd_abi << ", "
              << "avd_api=" << p.avd_api << ", "
              << "build_sdk=" << p.build_sdk << ", "
              << "build_id=" << p.build_id << ", "
              << "build_flavour=" << p.build_flavour << ", "
              << "emulator_full_version=" << EMULATOR_FULL_VERSION_STRING << ", "
              << "emulator_version=" << VERSION << ", "
              << "emulator_build_id=" << BUILD_ID << ", "
              << "emulator_platform=" << platform << ", "
              << "emulator_vk_icd=" << vk_icd;

    writer << p.avd_abi;
    writer << p.avd_api;
    writer << p.build_sdk;
    writer << p.build_id;
    writer << p.build_flavour;
    writer << std::string_view(EMULATOR_FULL_VERSION_STRING);
    writer << std::string_view(VERSION);
    writer << std::string_view(BUILD_ID);
    writer << platform;
    writer << vk_icd;

    class HwCfgWriterVisitor {
      public:
        HwCfgWriterVisitor(archive::IWriter& writer) : mWriter(writer) {}

        void operator()(const char* name, bool val) {
            if (!IsExcludedProp(name)) {
                mWriter << val;
                LOG(INFO) << "Saving HWCFG: " << name << "=" << val;
            } else {
                LOG(INFO) << "Not saving HWCFG: " << name << " (excluded)";
            }
        }
        void operator()(const char* name, int32_t val) {
            if (!IsExcludedProp(name)) {
                mWriter << val;
                LOG(INFO) << "Saving HWCFG: " << name << "=" << val;
            } else {
                LOG(INFO) << "Not saving HWCFG: " << name << " (excluded)";
            }
        }
        void operator()(const char* name, const std::string& val) {
            if (!IsExcludedProp(name)) {
                mWriter << val;
                LOG(INFO) << "Saving HWCFG: " << name << "=" << val;
            } else {
                LOG(INFO) << "Not saving HWCFG: " << name << " (excluded)";
            }
        }
        void operator()(const char* name, double val) {
            if (!IsExcludedProp(name)) {
                mWriter << val;
                LOG(INFO) << "Saving HWCFG: " << name << "=" << val;
            } else {
                LOG(INFO) << "Not saving HWCFG: " << name << " (excluded)";
            }
        }
        void operator()(const char* name, const android::goldfish::StorageCapacity& val) {
            if (!IsExcludedProp(name)) {
                mWriter << static_cast<uint64_t>(val.Bytes());
                LOG(INFO) << "Saving HWCFG: " << name << "=" << val.Bytes();
            } else {
                LOG(INFO) << "Not saving HWCFG: " << name << " (excluded)";
            }
        }

      private:
        archive::IWriter& mWriter;
    };

    HwCfgWriterVisitor visitor(writer);
    p.hw_config.Accept(visitor);
}

void AvdExtendedUniverse::OnSavePhysicalState(archive::IWriter& writer) const {
    // TODO: sensors_physical_model_
    writer << battery_ << guest_status_ << location_;
}

absl::Status AvdExtendedUniverse::OnLoad(archive::IReader& reader) {
    RETURN_IF_ERROR(OnLoadProps(reader));
    RETURN_IF_ERROR(OnLoadPhysicalState(reader));

    if (absl::Status s = GetMultiDisplay().Load(reader); !s.ok()) {
        LOG(WARNING) << "Failed to load multidisplay state: " << s;
        return s;
    }
    return absl::OkStatus();
}

absl::Status AvdExtendedUniverse::OnLoadProps(archive::IReader& reader) {
    const auto& p = Props();
    bool ok = true;

    auto check_int32 = [&](const char* name, int32_t val) {
        int32_t loaded = 0;
        if (const absl::Status s = ReadValue(reader, loaded); s.ok()) {
            if (loaded != val) {
                LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                             << ", expected: " << val << ")";
                ok = false;
            }
        } else {
            LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
            ok = false;
        }
    };
    auto check_str = [&](const char* name, const std::string& val) {
        std::string loaded;
        if (const absl::Status s = ReadValue(reader, loaded); s.ok()) {
            if (loaded != val) {
                LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                             << ", expected: " << val << ")";
                ok = false;
            }
        } else {
            LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
            ok = false;
        }
    };

    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    check_str("avd_abi", p.avd_abi);
    check_int32("avd_api", p.avd_api);
    check_str("build_sdk", p.build_sdk);
    check_str("build_id", p.build_id);
    check_str("build_flavour", p.build_flavour);
    check_str("emulator_full_version", EMULATOR_FULL_VERSION_STRING);
    check_str("emulator_version", VERSION);
    check_str("emulator_build_id", BUILD_ID);
    check_str("emulator_platform", std::string(platform));

    std::string current_vk_icd = GetCurrentVkIcd();
    std::string loaded_vk_icd;
    if (const absl::Status s = ReadValue(reader, loaded_vk_icd); s.ok()) {
        if (loaded_vk_icd != current_vk_icd) {
            LOG(WARNING) << "Property mismatch: emulator_vk_icd (loaded: " << loaded_vk_icd
                         << ", expected: " << current_vk_icd << ")";
            ok = false;
        }
    } else {
        LOG(WARNING) << "Could not load the 'emulator_vk_icd' property: " << s;
        ok = false;
    }

    class HwCfgReaderVisitor {
      public:
        HwCfgReaderVisitor(archive::IReader& reader, bool& ok) : mReader(reader), mOk(ok) {}

        void operator()(const char* name, bool val) {
            if (IsExcludedProp(name)) return;
            bool loaded = false;
            if (const absl::Status s = ReadValue(mReader, loaded); s.ok()) {
                if (loaded != val) {
                    LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                                 << ", expected: " << val << ")";
                    mOk = false;
                }
            } else {
                LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
                mOk = false;
            }
        }
        void operator()(const char* name, int32_t val) {
            if (IsExcludedProp(name)) return;
            int32_t loaded = 0;
            if (const absl::Status s = ReadValue(mReader, loaded); s.ok()) {
                if (loaded != val) {
                    LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                                 << ", expected: " << val << ")";
                    mOk = false;
                }
            } else {
                LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
                mOk = false;
            }
        }
        void operator()(const char* name, const std::string& val) {
            if (IsExcludedProp(name)) return;
            std::string loaded;
            if (const absl::Status s = ReadValue(mReader, loaded); s.ok()) {
                if (loaded != val) {
                    LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                                 << ", expected: " << val << ")";
                    mOk = false;
                }
            } else {
                LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
                mOk = false;
            }
        }
        void operator()(const char* name, double val) {
            if (IsExcludedProp(name)) return;
            double loaded = 0;
            if (const absl::Status s = ReadValue(mReader, loaded); s.ok()) {
                if (loaded != val) {
                    LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                                 << ", expected: " << val << ")";
                    mOk = false;
                }
            } else {
                LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
                mOk = false;
            }
        }
        void operator()(const char* name, const android::goldfish::StorageCapacity& val) {
            if (IsExcludedProp(name)) return;
            uint64_t loaded = 0;
            if (const absl::Status s = ReadValue(mReader, loaded); s.ok()) {
                if (loaded != val.Bytes()) {
                    LOG(WARNING) << "Property mismatch: " << name << " (loaded: " << loaded
                                 << ", expected: " << val.Bytes() << ")";
                    mOk = false;
                }
            } else {
                LOG(WARNING) << "Could not load the '" << name << "' property: " << s;
                mOk = false;
            }
        }

      private:
        archive::IReader& mReader;
        bool& mOk;
    };

    HwCfgReaderVisitor visitor(reader, ok);
    p.hw_config.Accept(visitor);

    if (!ok) {
        return absl::UnknownError("-1");
    }
    return absl::OkStatus();
}

absl::Status AvdExtendedUniverse::OnLoadPhysicalState(archive::IReader& reader) {
    // TODO: sensors_physical_model_
    return ReadValue(reader, battery_, guest_status_, location_);
}

absl::Status AvdExtendedUniverse::OnPostLoad() {
    guest_status_.OnPostLoad();
    return absl::OkStatus();
}

void AvdExtendedUniverse::Reset() {
    GetMultiDisplay().Reset();
}

}  // namespace goldfish::avd_info
