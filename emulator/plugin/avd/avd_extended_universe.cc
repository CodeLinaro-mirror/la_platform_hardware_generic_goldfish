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

#include "android/crashreport/crash_reporter.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/avd_info/gralloc_impl.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/vsock/clear.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
#include "system/reset.h"
#include "qemu/main-loop.h"
}
#undef listen
#undef shutdown
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::avd_info {

using devices::boot::EventLoop;

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

}  // namespace

AvdExtendedUniverse::AvdExtendedUniverse(std::unique_ptr<AvdProperties> props)
        : AvdUniverse(std::move(props)) {
    auto& avd_universe = *this;
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

    display::qemu_multidisplay::ConfigureMultiDisplay(client_loop, qemu_loop.get());

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

void AvdExtendedUniverse::OnPreSave() {
    // TODO
}

absl::Status AvdExtendedUniverse::OnSave(archive::IWriter&) const {
    // TODO
    return absl::OkStatus();
}

void AvdExtendedUniverse::OnPostSave() {
    // TODO
}

void AvdExtendedUniverse::OnPreLoad() {
    // TODO
}

absl::Status AvdExtendedUniverse::OnLoad(archive::IReader&) {
    // TODO
    return absl::OkStatus();
}

absl::Status AvdExtendedUniverse::OnPostLoad() {
    // TODO
    return absl::OkStatus();
}

}  // namespace goldfish::avd_info