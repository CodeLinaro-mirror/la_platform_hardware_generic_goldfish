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

#include <gtest/gtest.h>

#include "android/base/testing/test_system.h"
#include "android/goldfish/fake_hardware_config.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/async/testing/test_event_loop.h"

namespace goldfish::metrics {

using android::base::TestSystem;
using android_studio::AndroidStudioEvent;
using android_studio::EmulatorPerformanceStats;
using goldfish::async::testing::TestEventLoop;

TEST(PerfStatReporterTest, FillPerformanceStats) {
    TestSystem test_system("/tmp");
    TestSystem::Times times;
    times.user_ms = 100;
    times.system_ms = 200;
    times.wall_clock_ms = 3000;
    test_system.SetProcessTimes(times);

    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();
    hw.hw_ramSize = 1024;  // 1GB

    auto main_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops;

    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, "");

    AndroidStudioEvent event;
    reporter.FillEvent(event);
    const auto& stats = event.emulator_performance_stats();

    // Wall clock ms is converted to us
    EXPECT_EQ(stats.process_uptime_us(), 3000 * 1000);

    ASSERT_TRUE(stats.has_resource_usage());
    const auto& resource_usage = stats.resource_usage();

    ASSERT_TRUE(resource_usage.has_memory_usage());
    const auto& memory_usage = resource_usage.memory_usage();

    // TestSystem::GetMemUsage() returns these hardcoded values
    EXPECT_EQ(memory_usage.resident_memory(), 4294967295ULL);
    EXPECT_EQ(memory_usage.total_guest_memory(), 1024ULL * 1024 * 1024);
}

TEST(PerfStatReporterTest, FillMemUsageComplete) {
    TestSystem test_system("/tmp");
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();
    hw.hw_ramSize = 512;

    auto main_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops;
    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, "");

    AndroidStudioEvent event;
    reporter.FillEvent(event);
    const auto& memory_usage = event.emulator_performance_stats().resource_usage().memory_usage();

    // Verify all fields populated by FillMemUsage from TestSystem::GetMemUsage()
    EXPECT_EQ(memory_usage.resident_memory(), 4294967295ULL);
    EXPECT_EQ(memory_usage.resident_memory_max(), 4294967295ULL * 2);
    EXPECT_EQ(memory_usage.virtual_memory(), 4294967295ULL * 4);
    EXPECT_EQ(memory_usage.virtual_memory_max(), 4294967295ULL * 8);
    EXPECT_EQ(memory_usage.total_phys_memory(), 4294967295ULL * 16);
    EXPECT_EQ(memory_usage.total_page_file(), 4294967295ULL * 32);
    EXPECT_EQ(memory_usage.total_guest_memory(), 512ULL * 1024 * 1024);
}

TEST(PerfStatReporterTest, FillCpuUsageWithVCpus) {
    TestSystem test_system("/tmp");
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();

    auto main_loop = TestEventLoop::Create();
    auto vcpu1_loop = TestEventLoop::Create();
    auto vcpu2_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops = {vcpu1_loop.get(), vcpu2_loop.get()};

    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, "");

    AndroidStudioEvent event;
    reporter.FillEvent(event);
    const auto& stats = event.emulator_performance_stats();

    ASSERT_TRUE(stats.has_resource_usage());
    const auto& resource_usage = stats.resource_usage();

    EXPECT_TRUE(resource_usage.has_main_loop_slice());
    EXPECT_EQ(resource_usage.vcpu_slices_size(), 2);
}

TEST(PerfStatReporterTest, FillPerfStatsRamSizeZero) {
    TestSystem test_system("/tmp");
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();
    hw.hw_ramSize = 0;

    auto main_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops;
    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, "");

    AndroidStudioEvent event;
    reporter.FillEvent(event);
    const auto& memory_usage = event.emulator_performance_stats().resource_usage().memory_usage();

    EXPECT_FALSE(memory_usage.has_total_guest_memory());
}

TEST(PerfStatReporterTest, PeriodicReportToFile) {
    TestSystem test_system("/tmp");
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();

    auto main_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops;

    fs::path dump_file = test_system.GetTempRoot()->Path() / "metrics.txt";

    // std::fstream in PerfStatReporter requires the file to exist.
    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, dump_file);

    // Ensure ScheduleRepeating task is actually posted
    main_loop->RunAll();

    // Initial delay is 5s. Advance clock by 6s to trigger at least once.
    main_loop->AdvanceClock(absl::Seconds(6));

    // Run tasks that were triggered by AdvanceClock
    main_loop->RunAll();

    // Read the file and check if it has content.
    std::ifstream ifs(dump_file);
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string content = ss.str();

    EXPECT_FALSE(content.empty());
    // We expect "process_uptime_us" to be in the output as it is one of the fields.
    EXPECT_NE(content.find("process_uptime_us"), std::string::npos);
    EXPECT_NE(content.find("event time"), std::string::npos);
}

TEST(PerfStatReporterTest, FillEventMainLoopCpuUsageChange) {
    TestSystem test_system("/tmp");
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();

    auto main_loop = TestEventLoop::Create();
    std::vector<goldfish::async::EventLoop*> vcpu_loops;

    // Initial CPU time at zero
    android::base::CpuTime cpu_time;
    cpu_time.wall_time_us = 0;
    cpu_time.user_time_us = 0;
    cpu_time.system_time_us = 0;
    test_system.SetCpuTime(cpu_time);

    PerfStatReporter reporter(main_loop.get(), vcpu_loops, hw, "");

    // The reporter calls cpu_usage_.ScheduleUpdateNow() in constructor.
    // Run it to capture the initial CPU time (which is zero).
    main_loop->RunAll();

    // Now set some usage
    cpu_time.wall_time_us = 10000;
    cpu_time.user_time_us = 1000;
    cpu_time.system_time_us = 500;
    test_system.SetCpuTime(cpu_time);

    // Trigger an update
    main_loop->AdvanceClock(absl::Seconds(6));
    main_loop->RunAll();

    AndroidStudioEvent event;
    reporter.FillEvent(event);
    const auto& stats = event.emulator_performance_stats();
    const auto& resource_usage = stats.resource_usage();

    ASSERT_TRUE(resource_usage.has_main_loop_slice());
    const auto& main_loop_cpu = resource_usage.main_loop_slice();

    // We expect the deltas from zero: wall=10000, user=1000, system=500
    EXPECT_EQ(main_loop_cpu.wall_time_us(), 10000);
    EXPECT_EQ(main_loop_cpu.user_time_us(), 1000);
    EXPECT_EQ(main_loop_cpu.system_time_us(), 500);

    // Increment again
    cpu_time.wall_time_us += 20000;
    cpu_time.user_time_us += 2000;
    cpu_time.system_time_us += 1000;
    test_system.SetCpuTime(cpu_time);

    // Trigger update
    main_loop->AdvanceClock(absl::Seconds(6));
    main_loop->RunAll();

    AndroidStudioEvent event2;
    reporter.FillEvent(event2);
    const auto& main_loop_cpu2 =
            event2.emulator_performance_stats().resource_usage().main_loop_slice();

    // Deltas: 20000, 2000, 1000
    EXPECT_EQ(main_loop_cpu2.wall_time_us(), 20000);
    EXPECT_EQ(main_loop_cpu2.user_time_us(), 2000);
    EXPECT_EQ(main_loop_cpu2.system_time_us(), 1000);
}

}  // namespace goldfish::metrics
