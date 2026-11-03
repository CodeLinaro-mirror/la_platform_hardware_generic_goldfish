// Copyright (C) 2025 The Android Open Source Project
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

#include <filesystem>
#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace android::crashreport {

inline constexpr std::string_view kCrashpadHandlerEnvVar = "AEMU_CRASHPAD_HANDLER";
inline constexpr std::string_view kCrashDatabaseEnvVar = "ANDROID_EMU_CRASH_REPORTING_DATABASE";
inline constexpr std::string_view kEnableCrashReportingEnvVar =
        "ANDROID_EMU_ENABLE_CRASH_REPORTING";

enum class Consent {
    ASK,     ///< Defer upload and retain unuploaded local reports.
    ALWAYS,  ///< Upload local reports and remove uploaded entries.
    NEVER    ///< Delete unuploaded local reports without uploading.
};

class CrashSystem {
  public:
    struct Config {
        // Path to the `crashpad_handler` executable. Required when crash
        // reporting is enabled.
        std::filesystem::path handler_path;
        // Local Crashpad database directory. When empty, defaults to
        // `DatabaseDirectory()`.
        std::filesystem::path database_directory;

        // Builds a `Config` for child processes (such as QEMU or `:crash-me`)
        // whose `handler_path` and `database_directory` are passed via
        // environment variables.
        static Config FromEnv();
    };

    virtual ~CrashSystem() = default;

    virtual bool Active() const = 0;

    /**
     * @brief Initializes out-of-process Crashpad crash reporting.
     *
     * Starts `config.handler_path` and prepares `config.database_directory` (or
     * `DatabaseDirectory()` when unset, creating missing parent directories via
     * `EnsureDatabaseDirectory()`).
     *
     * Crash reporting is enabled by default unless
     * `ANDROID_EMU_ENABLE_CRASH_REPORTING` is set to `"0"` or `"false"`.
     *
     * @return OK if crash reporting is active or intentionally disabled via
     *     `ANDROID_EMU_ENABLE_CRASH_REPORTING`, otherwise an error describing
     *     why initialization failed, for the caller to log (Crashpad's own
     *     logging is not shown on Windows).
     */
    virtual absl::Status Initialize(const Config& config) = 0;

    virtual void UploadEntries(Consent consent) = 0;

    /**
     * @brief Returns the default local Crashpad database directory
     *     (`ANDROID_EMU_CRASH_REPORTING_DATABASE`, or `<temp>/emu-dev-crash-<ver>.db`).
     */
    static std::filesystem::path DatabaseDirectory();

    /**
     * @brief Creates the crash report database directory if it is missing.
     *
     * Crashpad only creates the last component of the database path, so a
     * missing parent directory (for example `C:\tmp` for `/tmp/crash-report.db`
     * on Windows) disables crash reporting. This creates the directory and any
     * missing parents, owner-only on POSIX.
     */
    static absl::StatusOr<std::filesystem::path> EnsureDatabaseDirectory(
            const std::filesystem::path& database_path);

    // Creates an isolated `CrashSystem` instance (useful in unit tests).
    static std::unique_ptr<CrashSystem> Create();

    // Returns the process-wide `CrashSystem` singleton.
    static CrashSystem& Get();
};

}  // namespace android::crashreport
