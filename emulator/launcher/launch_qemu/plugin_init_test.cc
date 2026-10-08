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

#include "plugin_init.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(PluginInitTest, DefaultLoggingEnv) {
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.emulator_paths().crashpad_handler_binary = "/default/bin/crashpad_handler";
    emu.emulator_paths().library_directory = "/default/lib/qemu";

    PluginInit dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()), IsEmpty());
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("AEMU_LOG_LEVEL", "1"), Pair("AEMU_VLOG_LEVEL", ""),
                                     Pair("AEMU_LOG_DETAILED", "false"),
                                     Pair("AEMU_CRASHPAD_HANDLER", "/default/bin/crashpad_handler"),
                                     Pair("ANDROID_EMULATOR_LIBRARY_DIR", "/default/lib/qemu")));
}

TEST(PluginInitTest, VerboseAndVmoduleLoggingEnv) {
    base::TestSystem sys("");
    char vlog_level[] = "4";
    char vmodule[] = "foo=2";
    AndroidOptions opts{
        .verbose = true,
        .V = vlog_level,
        .vmodule = vmodule,
        .wait_for_debugger = true,
        .debug_metrics = true,
    };
    FakeEmulator emu(opts);
    emu.emulator_paths().crashpad_handler_binary = "/default/bin/crashpad_handler";
    emu.emulator_paths().library_directory = "/default/lib/qemu";

    PluginInit dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("AEMU_LOG_LEVEL", "0"), Pair("AEMU_VLOG_LEVEL", "4"),
                                     Pair("AEMU_VMODULE", "foo=2,metrics_reporter=5"),
                                     Pair("AEMU_LOG_DETAILED", "true"),
                                     Pair("ANDROID_EMU_WAIT_FOR_DEBUGGER", "1"),
                                     Pair("AEMU_CRASHPAD_HANDLER", "/default/bin/crashpad_handler"),
                                     Pair("ANDROID_EMULATOR_LIBRARY_DIR", "/default/lib/qemu")));
}

TEST(PluginInitTest, PassthroughVarsAndIgnoresExistingAemuLoggingEnv) {
    base::TestSystem sys("");
    sys.EnvSet("AEMU_LOG_LEVEL", "2");
    sys.EnvSet("AEMU_VLOG_LEVEL", "3");
    sys.EnvSet("AEMU_VMODULE", "bar=1");
    sys.EnvSet("AEMU_LOG_DETAILED", "true");
    sys.EnvSet("AEMU_NO_LOG_SINK", "1");
    sys.EnvSet("ANDROID_EMU_ENABLE_CRASH_REPORTING", "YES");
    sys.EnvSet("ANDROID_EMU_CRASH_REPORTING_DATABASE", "/tmp/crash-report.db");
    sys.EnvSet("ANDROID_EMU_PERFETTO_TRACE", "1");
    sys.EnvSet("ANDROID_TMP", "/tmp/android-custom");

    FakeEmulator emu;
    emu.emulator_paths().crashpad_handler_binary = "/path/to/crashpad_handler";
    emu.emulator_paths().library_directory = "/path/to/lib/qemu";

    PluginInit dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(
                        Pair("AEMU_LOG_LEVEL", "1"), Pair("AEMU_VLOG_LEVEL", ""),
                        Pair("AEMU_LOG_DETAILED", "false"), Pair("AEMU_NO_LOG_SINK", "1"),
                        Pair("ANDROID_EMU_ENABLE_CRASH_REPORTING", "YES"),
                        Pair("ANDROID_EMU_CRASH_REPORTING_DATABASE", "/tmp/crash-report.db"),
                        Pair("ANDROID_EMU_PERFETTO_TRACE", "1"),
                        Pair("AEMU_CRASHPAD_HANDLER", "/path/to/crashpad_handler"),
                        Pair("ANDROID_TMP", "/tmp/android-custom"),
                        Pair("ANDROID_EMULATOR_LIBRARY_DIR", "/path/to/lib/qemu")));
}

}  // namespace android::goldfish::test
