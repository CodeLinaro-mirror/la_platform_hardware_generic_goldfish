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

#pragma once

#include "VCpuEventLoop.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/devices/battery/battery.h"
#include "goldfish/devices/boot/boot_properties_device.h"
#include "goldfish/devices/camera/register_device.h"
#include "goldfish/devices/clipboard/clipboard_device.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/fingerprint/fingerprint_device.h"
#include "goldfish/devices/gps/gps_device.h"
#include "goldfish/devices/guest_status/guest_status_device.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
#include "goldfish/devices/sensor/sensor_device.h"
#include "goldfish/devices/unix_pipe/unix_pipe.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/perf_stat_reporter.h"

namespace goldfish::avd_info {

struct AvdExtendedUniverse : public AvdUniverse {
    AvdExtendedUniverse(std::unique_ptr<AvdProperties> props);
    ~AvdExtendedUniverse() override;

    void WaitUntilEventLoopsIdle();
    void ShutdownQemuLoop();
    async::EventLoop& GetQemuEventLoop() override;
    metrics::MetricsReporter& GetMetricsReporter() override;

    void OnPreSave();
    absl::Status OnSave(archive::IWriter&) const;
    void OnPostSave();
    void OnPreLoad();
    absl::Status OnLoad(archive::IReader&);
    absl::Status OnPostLoad();

    devices::ConnectorRegistry connector_registry;
    devices::ConnectorRegistry test_tools_connector_registry;
    avd_universe::battery::ObservableBattery::ScopedCallbackHandle battery_subscription;
    avd_universe::guest_status::ObservableTimestamp::ScopedCallbackHandle bootcomplete_subscription;
    std::unique_ptr<metrics::MetricsReporter> metrics_reporter;
    std::shared_ptr<async::EventLoop::Timer> metrics_ping_timer;

    std::unique_ptr<async::EventLoop> qemu_event_loop;
    std::vector<VCpuEventLoop> qemu_cpu_loops;
    std::unique_ptr<metrics::PerfStatReporter> perf_stat_reporter;
    std::shared_ptr<async::EventLoop::Timer> perf_stat_reporter_task;
};

}  // namespace goldfish::avd_info
