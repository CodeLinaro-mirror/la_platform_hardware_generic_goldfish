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

#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace android::crashreport {

inline constexpr std::string_view kCrashpadHandlerEnvVar = "AEMU_CRASHPAD_HANDLER";
inline constexpr std::string_view kCrashDatabaseEnvVar = "ANDROID_EMU_CRASH_REPORTING_DATABASE";
inline constexpr std::string_view kEnableCrashReportingEnvVar =
        "ANDROID_EMU_ENABLE_CRASH_REPORTING";
inline constexpr std::string_view kMetricsSessionIdEnvVar = "AEMU_METRICS_SESSION_ID";

inline constexpr std::string_view kAnnotationProcessType = "process_type";
inline constexpr std::string_view kAnnotationSessionId = "session_id";

enum class Consent {
    ASK,     ///< Defer upload and retain unuploaded local reports.
    ALWAYS,  ///< Upload local reports and remove uploaded entries.
    NEVER    ///< Delete unuploaded local reports without uploading.
};

enum class ProcessType {
    kUnknown,
    kLauncher,
    kQemu,
};

struct CrashReportInfo {
    std::string uuid;
    time_t creation_time = 0;
    ProcessType process_type = ProcessType::kUnknown;
    std::filesystem::path minidump_path;
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
        // Optional process role recorded as the `process_type` Crashpad
        // annotation (`"launcher"` or `"qemu"`).
        ProcessType process_type = ProcessType::kUnknown;
        // Optional metrics session UUID recorded as the `session_id` Crashpad
        // annotation.
        std::string session_id;

        // Builds a `Config` for child processes (such as QEMU or `:crash-me`)
        // whose `handler_path`, `database_directory`, and `session_id` are
        // passed via environment variables.
        static Config FromEnv(ProcessType process_type = ProcessType::kUnknown);
    };

    virtual ~CrashSystem() = default;

    virtual bool Active() const = 0;

    /**
     * @brief Initializes out-of-process Crashpad crash reporting.
     *
     * Starts `config.handler_path`, prepares `config.database_directory` (or
     * `DatabaseDirectory()` when unset, creating missing parent directories via
     * `EnsureDatabaseDirectory()`), and attaches `process_type` / `session_id`
     * when present in `config`.
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

    /**
     * @brief Inspects pending and completed local minidump reports and returns
     *     metadata keyed by their `session_id` annotation.
     *
     * Call this before `UploadEntries()`, which deletes local reports when
     * upload consent is `Consent::NEVER`.
     */
    virtual absl::flat_hash_map<std::string, CrashReportInfo> GetReportsBySession() = 0;

    virtual void UploadEntries(Consent consent,
                               const absl::flat_hash_set<std::string>& retain_session_ids = {}) = 0;

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

    /**
     * @brief Reads key-value Crashpad annotations from a local minidump file.
     */
    static absl::StatusOr<absl::flat_hash_map<std::string, std::string>> ReadMinidumpAnnotations(
            const std::filesystem::path& minidump_path);

    // Creates an isolated `CrashSystem` instance (useful in unit tests).
    static std::unique_ptr<CrashSystem> Create();

    // Returns the process-wide `CrashSystem` singleton.
    static CrashSystem& Get();
};

}  // namespace android::crashreport
