// Copyright (C) 2026 The Android Open Source Project
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

#include "android/crashreport/crash_system.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/file/file.h"

namespace android::crashreport {
namespace {

namespace file = android::base::file;
namespace fs = std::filesystem;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::android::base::TestSystem;
using ::android::base::TestTempDir;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

class DisabledCrashReportingSystem : public TestSystem {
  public:
    DisabledCrashReportingSystem() : TestSystem("") {}
    bool GetEnableCrashReporting() const override { return false; }
};

TEST(CrashSystemTest, EnsureDatabaseDirectoryCreatesMissingParents) {
    TestTempDir temp_dir("crash_system_test");
    // Like /tmp/crash-report.db on a Windows drive without C:\tmp, under a non-ASCII directory.
    const fs::path missing_parent =
            temp_dir.Path() / fs::path(std::u8string_view(u8"täst_中文_🚀"));
    const fs::path database_path = missing_parent / "tmp" / "crash-report.db";

    ASSERT_OK_AND_ASSIGN(fs::path created, CrashSystem::EnsureDatabaseDirectory(database_path));

    EXPECT_TRUE(created.is_absolute());
    EXPECT_TRUE(file::is_dir(created));
    EXPECT_TRUE(file::is_dir(database_path));
#ifndef _WIN32
    ASSERT_OK_AND_ASSIGN(unsigned parent_mode, file::mode(missing_parent));
    EXPECT_EQ(parent_mode & 0777U, 0700U);
    ASSERT_OK_AND_ASSIGN(unsigned database_mode, file::mode(database_path));
    EXPECT_EQ(database_mode & 0777U, 0700U);
#endif
}

TEST(CrashSystemTest, EnsureDatabaseDirectoryAcceptsExistingDirectory) {
    TestTempDir temp_dir("crash_system_test");
    const fs::path database_path = temp_dir.Path() / "crash-report.db";
    ASSERT_TRUE(temp_dir.MakeSubDir("crash-report.db"));

    EXPECT_OK(CrashSystem::EnsureDatabaseDirectory(database_path));
    EXPECT_OK(CrashSystem::EnsureDatabaseDirectory(database_path));
    EXPECT_TRUE(file::is_dir(database_path));
}

TEST(CrashSystemTest, EnsureDatabaseDirectoryRejectsRegularFile) {
    TestTempDir temp_dir("crash_system_test");
    const fs::path database_path = temp_dir.Path() / "crash-report.db";
    ASSERT_TRUE(temp_dir.MakeSubFile("crash-report.db"));

    EXPECT_THAT(CrashSystem::EnsureDatabaseDirectory(database_path),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         AllOf(HasSubstr(database_path.string()),
                               HasSubstr("exists but is not a directory"))));
    EXPECT_TRUE(file::is_file(database_path));
}

TEST(CrashSystemTest, EnsureDatabaseDirectoryReportsPathItCannotCreate) {
#ifdef _WIN32
    // mkdir_recursive() calls the throwing fs::exists(), which recent Windows
    // versions can make throw for a path below a regular file.
    GTEST_SKIP() << "Runs on POSIX only.";
#endif
    TestTempDir temp_dir("crash_system_test");
    ASSERT_TRUE(temp_dir.MakeSubFile("not-a-directory"));
    const fs::path database_path = temp_dir.Path() / "not-a-directory" / "crash-report.db";

    auto created = CrashSystem::EnsureDatabaseDirectory(database_path);

    ASSERT_FALSE(created.ok());
    EXPECT_THAT(created.status().message(),
                AllOf(HasSubstr("Cannot create crash report database directory"),
                      HasSubstr(database_path.string()),
                      HasSubstr(kCrashDatabaseEnvVar)));
}

TEST(CrashSystemTest, ConfigFromEnvReadsChildProcessVariablesWithNonAsciiPaths) {
    TestSystem sys("");
    const fs::path ascii_or_latin_handler = fs::path("/sdk/Télévision/crashpad_handler");
    const fs::path ascii_or_latin_db = fs::path("/tmp/Télévision/crash.db");
    sys.EnvSet(std::string(kCrashpadHandlerEnvVar), ascii_or_latin_handler.string());
    sys.EnvSet(std::string(kCrashDatabaseEnvVar), ascii_or_latin_db.string());
    sys.EnvSet(std::string(kMetricsSessionIdEnvVar), "11111111-2222-3333-4444-555555555555");

    CrashSystem::Config config = CrashSystem::Config::FromEnv(ProcessType::kQemu);
    EXPECT_EQ(config.handler_path, ascii_or_latin_handler);
    EXPECT_EQ(config.database_directory, ascii_or_latin_db);
    EXPECT_EQ(config.process_type, ProcessType::kQemu);
    EXPECT_EQ(config.session_id, "11111111-2222-3333-4444-555555555555");

    // Some Windows CI bots still run Windows Server 2019 (build 10.0.17763),
    // which ignores <activeCodePage>UTF-8</activeCodePage> and falls back to
    // CP1252 where wide CJK/emoji paths cannot convert via path::string().
    if (android::base::System::IsUtf8CodePage()) {
        const fs::path utf8_handler =
                fs::path("/sdk") / fs::path(std::u8string_view(u8"测试_🚀_é")) / "crashpad_handler";
        const fs::path utf8_db =
                fs::path("/tmp") / fs::path(std::u8string_view(u8"测试_🚀_é")) / "crash.db";
        sys.EnvSet(std::string(kCrashpadHandlerEnvVar), utf8_handler.string());
        sys.EnvSet(std::string(kCrashDatabaseEnvVar), utf8_db.string());

        config = CrashSystem::Config::FromEnv(ProcessType::kQemu);
        EXPECT_EQ(config.handler_path, utf8_handler);
        EXPECT_EQ(config.database_directory, utf8_db);
    }
}

TEST(CrashSystemTest, InitializeReturnsNotFoundWhenHandlerPathIsEmpty) {
    TestSystem sys("");
    auto crash_system = CrashSystem::Create();

    EXPECT_THAT(crash_system->Initialize({.process_type = ProcessType::kLauncher}),
                StatusIs(absl::StatusCode::kNotFound, HasSubstr(kCrashpadHandlerEnvVar)));
    EXPECT_FALSE(crash_system->Active());
}

TEST(CrashSystemTest, ReadMinidumpAnnotationsReturnsErrorOnMissingOrInvalidFile) {
    TestTempDir temp_dir("crash_system_minidump_test");
    const fs::path missing_file = temp_dir.Path() / "missing.dmp";
    EXPECT_THAT(CrashSystem::ReadMinidumpAnnotations(missing_file),
                StatusIs(absl::StatusCode::kNotFound, HasSubstr(missing_file.string())));

    ASSERT_TRUE(temp_dir.MakeSubFile("corrupt.dmp"));
    const fs::path corrupt_file = temp_dir.Path() / "corrupt.dmp";
    EXPECT_THAT(CrashSystem::ReadMinidumpAnnotations(corrupt_file),
                StatusIs(absl::StatusCode::kDataLoss, HasSubstr(corrupt_file.string())));
}

TEST(CrashSystemTest, InitializeSucceedsInactiveWhenCrashReportingDisabled) {
    DisabledCrashReportingSystem sys;
    TestTempDir temp_dir("crash_system_disabled");
    const fs::path missing_db = temp_dir.Path() / "nonexistent_db";
    auto crash_system = CrashSystem::Create();

    EXPECT_THAT(crash_system->Initialize({.database_directory = missing_db}), IsOk());
    EXPECT_FALSE(crash_system->Active());
    EXPECT_THAT(crash_system->GetReportsBySession(), IsEmpty());
}

}  // namespace
}  // namespace android::crashreport
