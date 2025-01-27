// Copyright 2015 The Android Open Source Project
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
#include <sys/cdefs.h>

__BEGIN_DECLS

/**
 * @brief Initializes the crash reporting system.
 *
 * This function sets up crash reporting for the emulator. It should be called
 * only once at program startup. This function attempts to start the
 * out-of-process Crashpad handler. If the handler is successfully started,
 * crashes will be recorded in a local database and can be uploaded later. If
 * the Crashpad handler cannot be started, crash reporting will be disabled, and
 * a warning will be logged.
 *
 * Internally, this function utilizes the `CrashpadClient::StartHandler()`
 * method to establish communication with the Crashpad handler and configure
 * exception handling. Refer to the documentation of `StartHandler()` for
 * platform-specific details on how the handler is started and how exception
 * ports or signal handlers are configured.
 *
 * Crash reporting is controlled by the `ANDROID_EMU_ENABLE_CRASH_REPORTING`
 * environment variable. If this variable is not set or is set to "0", crash
 * reporting will be disabled.
 *
 * @param argc The number of command line arguments. These are used to populate
 *     the crash report if a crash occurs.
 * @param argv An array of command line arguments. These are used to populate
 *     the crash report if a crash occurs.
 * @return True if crash reporting was successfully initialized, false otherwise.
 *
 * @note If the Crashpad handler cannot be found, crash reporting will be
 *     disabled. The handler location can be influenced by setting the
 *     `ANDROID_EMU_CRASHPAD_HANDLER` environment variable. If not set, the
 *     following locations are searched (in order):
 *     1. The Bazel runfiles directory (if building with Bazel).
 *     2. The emulator's `bin/` directory (if building a release).
 *
 * @note The crash report database is stored in a platform-specific temporary
 *     directory by default, and its name is based on the emulator version.
 *     The location can be overridden by setting the
 *     `ANDROID_EMU_CRASH_REPORTING_DATABASE` environment variable.
 *
 * @note On macOS every child process will be handled by this reporter, on every
 *     other platform a reporter will need to be registered by the client.
 */
bool crashhandler_init(int argc, char** argv);

__END_DECLS
