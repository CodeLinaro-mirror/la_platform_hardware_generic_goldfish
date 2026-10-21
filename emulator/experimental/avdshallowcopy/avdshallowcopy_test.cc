// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"

#include "android/base/bazel_info.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/process/command.h"
#include "goldfish/file/file.h"
#include "goldfish/file/storage_capacity.h"
#include "goldfish/fsimage/qcow2.h"

namespace {

namespace fs = std::filesystem;

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::android::base::TestTempDir;
using ::android::base::operator""_MiB;

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

fs::path FindAvdShallowCopyBinary() {
    return android::base::Bazel::RunfilesPath(
            absl::StrCat("goldfish+/emulator/experimental/avdshallowcopy/avdshallowcopy", EXE));
}

void WriteTextFile(const fs::path& path, std::string_view content) {
    std::ofstream ofs(path, std::ios_base::binary | std::ios_base::trunc);
    ASSERT_TRUE(ofs.good());
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ASSERT_TRUE(ofs.good());
}

TEST(AvdShallowCopyTest, ShallowCopiesIniAndAvdDirectory) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();
    const fs::path src_avd = avd_dir / "Pixel_8.avd";
    const fs::path dst_avd = avd_dir / "Pixel_8_Clone.avd";
    ASSERT_THAT(android::base::file::mkdir_recursive(src_avd / "snapshots" / "default_boot", 0755),
                IsOk());

    WriteTextFile(avd_dir / "Pixel_8.ini",
                  absl::StrCat("avd.ini.encoding=UTF-8\n", "path=", src_avd.string(), "\n",
                               "path.rel=avd/Pixel_8.avd\n", "target=android-37.1\n",
                               "extra.key=extra.value\n"));

    WriteTextFile(src_avd / "config.ini",
                  "AvdId=Pixel_8\navd.ini.displayname=Pixel 8\nhw.ramSize=2048\n");
    WriteTextFile(src_avd / "userdata.img", "raw userdata image content");
    WriteTextFile(src_avd / "snapshots" / "default_boot" / "ram.img", "raw snapshot ram");
    WriteTextFile(src_avd / "snapshots" / "default_boot" / "snapshot.pb", "proto bytes");
    ASSERT_THAT(goldfish::fsimage::CreateQcow2Overlay(src_avd / "userdata.img.qcow2", 16_MiB),
                IsOk());

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto proc = android::base::Command::Create(
                        {binary.string(), avd_dir.string(), "Pixel_8", "Pixel_8_Clone"})
                        .RedirectStdoutToUnsafe(&stdout_buf)
                        .RedirectStderrToUnsafe(&stderr_buf)
                        .Execute();
    ASSERT_EQ(proc->ExitCode(), 0) << proc->Err()->AsString();

    // Verify DST_NAME.ini
    EXPECT_THAT(android::base::file::read_whole_file(avd_dir / "Pixel_8_Clone.ini", false),
                IsOkAndHolds(absl::StrCat("avd.ini.encoding = UTF-8\n", "path = ", dst_avd.string(),
                                          "\n", "path.rel = avd/Pixel_8_Clone.avd\n",
                                          "target = android-37.1\n", "extra.key = extra.value\n")));

    // Verify config.ini updated with DST_NAME
    EXPECT_THAT(android::base::file::read_whole_file(dst_avd / "config.ini", false),
                IsOkAndHolds("AvdId = Pixel_8_Clone\n"
                             "avd.ini.displayname = Pixel_8_Clone\n"
                             "hw.ramSize = 2048\n"));

    // Verify regular files copied as-is
    EXPECT_THAT(android::base::file::read_whole_file(
                        dst_avd / "snapshots" / "default_boot" / "snapshot.pb", true),
                IsOkAndHolds("proto bytes"));

    // Verify .img files replaced with "Placeholder: SOURCE_PATH"
    EXPECT_THAT(android::base::file::read_whole_file(dst_avd / "userdata.img", true),
                IsOkAndHolds(absl::StrCat("Placeholder: ", (src_avd / "userdata.img").string())));
    EXPECT_THAT(android::base::file::read_whole_file(
                        dst_avd / "snapshots" / "default_boot" / "ram.img", true),
                IsOkAndHolds(absl::StrCat(
                        "Placeholder: ",
                        (src_avd / "snapshots" / "default_boot" / "ram.img").string())));

    // Verify .qcow2 files point to the source .qcow2 file
    EXPECT_TRUE(goldfish::fsimage::IsValidQcow2Image(dst_avd / "userdata.img.qcow2"));
    EXPECT_THAT(goldfish::fsimage::ReadQcow2BackingPath(dst_avd / "userdata.img.qcow2"),
                IsOkAndHolds(src_avd / "userdata.img.qcow2"));
}

TEST(AvdShallowCopyTest, FailsWhenSourceConfigIniDoesNotExist) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();
    ASSERT_THAT(android::base::file::mkdir(avd_dir / "Src.avd", 0755), IsOk());
    WriteTextFile(avd_dir / "Src.ini",
                  "avd.ini.encoding=UTF-8\npath=/tmp/Src.avd\npath.rel=avd/"
                  "Src.avd\ntarget=android-37.1\n");

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto proc = android::base::Command::Create({binary.string(), avd_dir.string(), "Src", "Dst"})
                        .RedirectStdoutToUnsafe(&stdout_buf)
                        .RedirectStderrToUnsafe(&stderr_buf)
                        .Execute();
    EXPECT_NE(proc->ExitCode(), 0);
    EXPECT_THAT(proc->Err()->AsString(), ::testing::HasSubstr("config.ini"));
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.ini"));
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.avd"));
}

TEST(AvdShallowCopyTest, FailsWhenDestinationIniAlreadyExists) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();
    ASSERT_THAT(android::base::file::mkdir(avd_dir / "Src.avd", 0755), IsOk());
    WriteTextFile(avd_dir / "Src.avd" / "config.ini", "AvdId=Src\n");
    WriteTextFile(avd_dir / "Src.ini",
                  "avd.ini.encoding=UTF-8\npath=/tmp/Src.avd\npath.rel=avd/"
                  "Src.avd\ntarget=android-37.1\n");
    WriteTextFile(avd_dir / "Dst.ini", "existing dst ini");

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto proc = android::base::Command::Create({binary.string(), avd_dir.string(), "Src", "Dst"})
                        .RedirectStdoutToUnsafe(&stdout_buf)
                        .RedirectStderrToUnsafe(&stderr_buf)
                        .Execute();
    EXPECT_NE(proc->ExitCode(), 0);
    EXPECT_THAT(proc->Err()->AsString(), ::testing::HasSubstr("already exists"));
    EXPECT_THAT(android::base::file::read_whole_file(avd_dir / "Dst.ini", true),
                IsOkAndHolds("existing dst ini"));
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.avd"));
}

TEST(AvdShallowCopyTest, FailsWhenDestinationAvdDirectoryAlreadyExists) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();
    ASSERT_THAT(android::base::file::mkdir(avd_dir / "Src.avd", 0755), IsOk());
    WriteTextFile(avd_dir / "Src.avd" / "config.ini", "AvdId=Src\n");
    ASSERT_THAT(android::base::file::mkdir(avd_dir / "Dst.avd", 0755), IsOk());
    WriteTextFile(avd_dir / "Src.ini",
                  "avd.ini.encoding=UTF-8\npath=/tmp/Src.avd\npath.rel=avd/"
                  "Src.avd\ntarget=android-37.1\n");

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto proc = android::base::Command::Create({binary.string(), avd_dir.string(), "Src", "Dst"})
                        .RedirectStdoutToUnsafe(&stdout_buf)
                        .RedirectStderrToUnsafe(&stderr_buf)
                        .Execute();
    EXPECT_NE(proc->ExitCode(), 0);
    EXPECT_THAT(proc->Err()->AsString(), ::testing::HasSubstr("already exists"));
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.ini"));
}

TEST(AvdShallowCopyTest, CleansUpDestinationOnCopyFailure) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();
    const fs::path src_avd = avd_dir / "Src.avd";
    ASSERT_THAT(android::base::file::mkdir(src_avd, 0755), IsOk());
    WriteTextFile(src_avd / "config.ini", "AvdId=Src\n");
    WriteTextFile(avd_dir / "Src.ini",
                  "avd.ini.encoding=UTF-8\npath=/tmp/Src.avd\npath.rel=avd/"
                  "Src.avd\ntarget=android-37.1\n");
    // Truncated qcow2 magic causes CreateQcow2Overlay to fail during directory copy.
    WriteTextFile(src_avd / "corrupt.qcow2", "QFI\xFB");

    std::stringbuf stdout_buf;
    std::stringbuf stderr_buf;
    auto proc = android::base::Command::Create({binary.string(), avd_dir.string(), "Src", "Dst"})
                        .RedirectStdoutToUnsafe(&stdout_buf)
                        .RedirectStderrToUnsafe(&stderr_buf)
                        .Execute();
    EXPECT_NE(proc->ExitCode(), 0);
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.ini"));
    EXPECT_FALSE(android::base::file::exists(avd_dir / "Dst.avd"));
}

TEST(AvdShallowCopyTest, FailsWhenAvdNameIsNotSimpleName) {
    const fs::path binary = FindAvdShallowCopyBinary();
    ASSERT_FALSE(binary.empty());

    TestTempDir temp_dir("avdshallowcopy_test");
    const fs::path avd_dir = temp_dir.Path();

    for (const auto& [src, dst] : std::initializer_list<std::pair<std::string, std::string>>{
             {"../Src", "Dst"},
             {"Src", "../Dst"},
             {"sub/Src", "Dst"},
             {"Src", "sub/Dst"},
             {".", "Dst"},
             {"Src", ".."},
         }) {
        std::stringbuf stdout_buf;
        std::stringbuf stderr_buf;
        auto proc = android::base::Command::Create({binary.string(), avd_dir.string(), src, dst})
                            .RedirectStdoutToUnsafe(&stdout_buf)
                            .RedirectStderrToUnsafe(&stderr_buf)
                            .Execute();
        EXPECT_NE(proc->ExitCode(), 0) << "src=" << src << " dst=" << dst;
        EXPECT_THAT(proc->Err()->AsString(), ::testing::HasSubstr("Invalid AVD name"));
    }
}

}  // namespace
