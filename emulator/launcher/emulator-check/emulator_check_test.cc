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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>

#include "absl/strings/str_cat.h"

#include "android/base/bazel_info.h"
#include "android/process/command.h"

namespace fs = std::filesystem;

#ifdef _WIN32
#define EXE ".exe"
#define LINE_END "\r\n"
#else
#define EXE ""
#define LINE_END "\n"
#endif

fs::path FindEmulatorCheckBinary() {
    return android::base::Bazel::RunfilesPath(
            absl::StrCat("goldfish+/emulator/launcher/emulator-check/emulator-check", EXE));
}

TEST(EmulatorCheckTest, noArgumentsFails) {
    fs::path binary = FindEmulatorCheckBinary();
    ASSERT_FALSE(binary.empty()) << "Could not find emulator-check binary in runfiles!";

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto p = android::base::Command::Create({binary.string()})
                     .RedirectStdoutToUnsafe(&stdout_buf)
                     .RedirectStderrToUnsafe(&stderr_buf)
                     .Execute();
    EXPECT_EQ(p->ExitCode(), 100);
    EXPECT_THAT(p->Err()->AsString(), ::testing::HasSubstr("Missing a required argument(s)"));
}

TEST(EmulatorCheckTest, helpOptionSucceeds) {
    fs::path binary = FindEmulatorCheckBinary();
    ASSERT_FALSE(binary.empty()) << "Could not find emulator-check binary in runfiles!";

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto p = android::base::Command::Create({binary.string(), "-help"})
                     .RedirectStdoutToUnsafe(&stdout_buf)
                     .RedirectStderrToUnsafe(&stderr_buf)
                     .Execute();
    EXPECT_EQ(p->ExitCode(), 0);
    EXPECT_THAT(p->Out()->AsString(), ::testing::HasSubstr("Usage: emulator-check"));
}

TEST(EmulatorCheckTest, unknownArgumentFormat) {
    fs::path binary = FindEmulatorCheckBinary();
    ASSERT_FALSE(binary.empty()) << "Could not find emulator-check binary in runfiles!";

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto p = android::base::Command::Create({binary.string(), "some_unknown_arg"})
                     .RedirectStdoutToUnsafe(&stdout_buf)
                     .RedirectStderrToUnsafe(&stderr_buf)
                     .Execute();
    EXPECT_EQ(p->ExitCode(), 100);
    // Standard format check:
    // Line 1: some_unknown_arg:
    // Line 2: 100
    // Line 3: Unknown argument
    // Line 4: some_unknown_arg
    std::string out = p->Out()->AsString();
    EXPECT_THAT(out, ::testing::HasSubstr("some_unknown_arg:" LINE_END "100" LINE_END
                                          "Unknown argument" LINE_END "some_unknown_arg" LINE_END));
}

TEST(EmulatorCheckTest, accelRunsAndOutputsCorrectFormat) {
    fs::path binary = FindEmulatorCheckBinary();
    ASSERT_FALSE(binary.empty()) << "Could not find emulator-check binary in runfiles!";

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto p = android::base::Command::Create({binary.string(), "accel"})
                     .RedirectStdoutToUnsafe(&stdout_buf)
                     .RedirectStderrToUnsafe(&stderr_buf)
                     .Execute();
    int code = p->ExitCode();
    std::string out = p->Out()->AsString();
    std::string err = p->Err()->AsString();

    EXPECT_THAT(out, ::testing::HasSubstr("accel:" LINE_END))
            << "Exit code: " << code << ", Stderr: " << err;
    EXPECT_THAT(out, ::testing::HasSubstr(LINE_END "accel" LINE_END));
}
