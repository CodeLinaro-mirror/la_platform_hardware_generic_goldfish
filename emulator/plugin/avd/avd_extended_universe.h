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

#include <memory>

#include "absl/status/statusor.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/avd/avd_universe.h"
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
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/perf_stat_reporter.h"
#include "goldfish/metrics/snapshot_metrics.h"
#include "vcpu_event_loop.h"

namespace goldfish::avd {

struct AvdExtendedUniverse : public AvdUniverse {
  private:
    struct Private {};

  public:
    static absl::StatusOr<std::unique_ptr<AvdExtendedUniverse>> Create(
            std::unique_ptr<AvdProperties> props);
    AvdExtendedUniverse(Private, std::unique_ptr<AvdProperties> props);

    ~AvdExtendedUniverse() override;

    void WaitUntilEventLoopsIdle();
    void ShutdownQemuLoop();
    async::EventLoop& GetQemuEventLoop() override;
    metrics::MetricsReporter& GetMetricsReporter() override;
    android::goldfish::VmOperations& GetVmOperations() override;
    display::IMultiDisplay& GetMultiDisplay() const override;

    void OnPreSave() override;
    absl::Status OnSave(archive::IWriter&) const override;
    void OnPreLoad() override;
    absl::Status OnLoad(archive::IReader&) override;
    absl::Status OnPostLoad() override;

    void Reset();

    std::unique_ptr<async::EventLoop> qemu_event_loop;
    std::vector<VCpuEventLoop> qemu_cpu_loops;
    std::unique_ptr<android::goldfish::VmOperations> vm_operations;

    std::unique_ptr<display::IMultiDisplay> multi_display;
    display::IMultiDisplay::ScopedRotationSubscription display_rotation_subscription;

    devices::ConnectorRegistry connector_registry;
    devices::ConnectorRegistry test_tools_connector_registry;
    avd_universe::battery::ObservableBattery::ScopedCallbackHandle battery_subscription;
    std::unique_ptr<metrics::MetricsReporter> metrics_reporter;
    std::shared_ptr<async::EventLoop::Timer> metrics_ping_timer;

    std::unique_ptr<metrics::PerfStatReporter> perf_stat_reporter;
    std::shared_ptr<async::EventLoop::Timer> perf_stat_reporter_task;

  private:
    absl::Status Initialize();

    void OnSaveProps(archive::IWriter&) const;
    void OnSavePhysicalState(archive::IWriter&) const;
    absl::Status OnSaveDisplayState(archive::IWriter&) const;

    absl::Status OnLoadProps(archive::IReader&);
    absl::Status OnLoadPhysicalState(archive::IReader&);
    absl::Status OnLoadDisplayState(archive::IReader&);

    void SyncGuestTimeToHost();

    metrics::QuickbootMetricsState quickboot_metrics_;
};

}  // namespace goldfish::avd
