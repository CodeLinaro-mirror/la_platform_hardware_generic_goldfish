/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/metrics/perf_stat_reporter.h"

#include "android/base/memory.h"
#include "android/base/system.h"

namespace goldfish::metrics {

namespace {

void FillMemUsage(const android::base::System& system, const android::goldfish::HardwareConfig& hw,
                  android_studio::EmulatorMemoryUsage* mem_usage_proto) {
    const android::base::MemUsage raw_mem_usage = system.GetMemUsage();
    mem_usage_proto->set_resident_memory(raw_mem_usage.resident);
    mem_usage_proto->set_resident_memory_max(raw_mem_usage.resident_max);
    mem_usage_proto->set_virtual_memory(raw_mem_usage.virt);
    mem_usage_proto->set_virtual_memory_max(raw_mem_usage.virt_max);
    mem_usage_proto->set_total_phys_memory(raw_mem_usage.total_phys_memory);
    mem_usage_proto->set_total_page_file(raw_mem_usage.total_page_file);

    if (hw.hw_ramSize > 0) {
        mem_usage_proto->set_total_guest_memory(hw.hw_ramSize * 1024 * 1024);
    }
}

void FillCpuUsage(const CpuUsage& cpu_usage, android_studio::EmulatorResourceUsage* resources) {
    cpu_usage.UseMainCpuUsage([resources](android::base::CpuTime cpu_time) {
        auto* main_loop_cpu = resources->mutable_main_loop_slice();
        main_loop_cpu->set_wall_time_us(cpu_time.wall_time_us);
        main_loop_cpu->set_user_time_us(cpu_time.user_time_us);
        main_loop_cpu->set_system_time_us(cpu_time.system_time_us);
    });

    cpu_usage.ForEachVCpuUsage([resources](android::base::CpuTime cpu_time) {
        auto* v_cpu = resources->add_vcpu_slices();
        v_cpu->set_wall_time_us(cpu_time.wall_time_us);
        v_cpu->set_user_time_us(cpu_time.user_time_us);
        v_cpu->set_system_time_us(cpu_time.system_time_us);
    });
}

}  // namespace

void PerfStatReporter::FillPerfStats(const android::goldfish::HardwareConfig& hw,
                                     const CpuUsage& cpu_usage,
                                     android_studio::EmulatorPerformanceStats* stats_out) {
    const auto& system = *android::base::System::Get();
    const auto times = system.GetProcessTimes();
    // process_uptime_us is usually wall clock time since start.
    stats_out->set_process_uptime_us(times.wall_clock_ms * 1000);

    // TODO stats_out->set_guest_uptime_us(x * 1000);

    auto* resources = stats_out->mutable_resource_usage();
    FillMemUsage(system, hw, resources->mutable_memory_usage());
    FillCpuUsage(cpu_usage, resources);
}

}  // namespace goldfish::metrics
