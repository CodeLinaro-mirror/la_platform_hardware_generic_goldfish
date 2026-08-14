// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/goldfish/avd.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"

#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/goldfish/input_paths.h"
#include "android/status/status_matcher_macros.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using android::base::TestSystem;
using android::base::TestTempDir;

namespace android::goldfish::avd {

namespace fs = std::filesystem;

class AvdTest : public ::testing::Test {
  protected:
    void SetUp() override {
        sys_ = std::make_unique<TestSystem>("/home", "/");
        tmp_ = sys_->GetTempRoot();
        paths_ = SetupPaths(tmp_);
    }

    void WriteToFile(const fs::path& path, std::string_view text) {
        std::ofstream iniFile(path, std::ios::trunc);
        iniFile << text;
    }

    UserPaths SetupPaths(TestTempDir* tmp) {
        fs::path android_home("android_home");
        tmp->MakeSubDir(android_home);
        tmp->MakeSubDir(android_home / "avd");
        tmp->MakeSubDir(android_home / "sysimg");
        fs::path sysimg = tmp->Path() / android_home / "sysimg";
        WriteToFile(sysimg / "build.prop",
                    "ro.system.build.version.sdk=30\nro.product.cpu.abi=x86_64");
        WriteToFile(sysimg / "advancedFeatures.ini", "");
        WriteToFile(sysimg / "VerifiedBootParams.textproto", "");
        tmp->MakeSubDir(android_home / "sysimg" / "data");
        WriteToFile(sysimg / "kernel_cmdline.txt", "");
        WriteToFile(sysimg / "kernel-ranchu", "");
        WriteToFile(sysimg / "ramdisk.img", "");
        WriteToFile(sysimg / "system.img", "");
        WriteToFile(sysimg / "vendor.img", "");
        WriteToFile(sysimg / "encryptionkey.img", "");

        return {
            .user_directory = tmp->Path(),
            .avd_directory = tmp->Path() / android_home / "avd",
            .sdk_directory = tmp->Path() / android_home,
        };
    }

    fs::path CreateTestAvd(const std::string& name, const std::string& targetString,
                           int api_level) {
        fs::path avd_dir = paths_.avd_directory / (name + ".avd");
        base::file::mkdir_recursive(avd_dir, 0755).IgnoreError();

        // Create an ini file for the test AVD
        WriteToFile(paths_.avd_directory / (name + ".ini"),
                    absl::StrCat("path=", avd_dir.string()));

        // Set the 'target' property in the config.ini file
        WriteToFile(avd_dir / "config.ini",
                    absl::StrCat("target=", targetString, "\nimage.sysdir.1=sysimg"));
        WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                    absl::StrCat("ro.system.build.version.sdk=", api_level,
                                 "\nro.product.cpu.abi=x86_64"));

        return avd_dir;
    }

    std::unique_ptr<TestSystem> sys_;
    TestTempDir* tmp_;
    UserPaths paths_;
    AndroidOptions opts_ = {};
};

TEST_F(AvdTest, ApiLevel) {
    CreateTestAvd("test_avd", "android-30", 30);
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->ApiLevel(), 30);
}

TEST_F(AvdTest, Dessert) {
    CreateTestAvd("test_avd", "android-30", 30);
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->Dessert(), "R");
}

TEST_F(AvdTest, ApiDescriptionDecimalVersions) {
    {
        CreateTestAvd("avd_35", "android-35", 35);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_35", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "15.0 (V) - API 35");
    }
    {
        CreateTestAvd("avd_36_1", "android-36.1", 36);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_36_1", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "16 (B) - API 36.1");
    }
    {
        CreateTestAvd("avd_37_0", "android-37.0", 37);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_37_0", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "17 (C) - API 37.0");
    }
    {
        CreateTestAvd("avd_37_1", "android-37.1", 37);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_37_1", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "17 (C) - API 37.1");
    }
    {
        CreateTestAvd("avd_37_2_beta1", "android-37.2-beta1", 37);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_37_2_beta1", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "17 (C) - API 37.2-beta1");
    }
    {
        CreateTestAvd("avd_baklava", "android-Baklava", 36);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_baklava", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "16 (B) - API Baklava");
    }
    {
        CreateTestAvd("avd_cinnamon", "android-CinnamonBun", 37);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_cinnamon", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "17 (C) - API CinnamonBun");
    }
    {
        CreateTestAvd("avd_addon_37_1", "Google Inc.:Google APIs:37.1", 37);
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "avd_addon_37_1", false, ""));
        EXPECT_EQ(avd->ApiDescription(), "17 (C) - API 37.1");
    }
}

TEST_F(AvdTest, UnknownApiLevel) {
    CreateTestAvd("test_avd", "android-1000", 1000);
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->ApiLevel(), 1000);
    EXPECT_EQ(avd->Dessert(), "");
}

TEST_F(AvdTest, InvalidTargetFormat) {
    CreateTestAvd("test_avd", "invalid-target-format", 30);
    // overwrite build.prop
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.system.build.version.sdk=foo\nro.product.cpu.abi=x86_64");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->ApiLevel(), Avd::kUnknownApiLevel);
    EXPECT_EQ(avd->Dessert(), "");
}

TEST_F(AvdTest, AvdNotFound) {
    EXPECT_THAT(Avd::FromName(opts_, paths_, "non_existent", false, ""),
                StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(AvdTest, ListMultipleAvds) {
    CreateTestAvd("avd1", "android-30", 30);
    CreateTestAvd("avd2", "android-30", 30);

    // Invalid AVD name (should be ignored by List)
    fs::path invalid_ini = paths_.avd_directory / "invalid name.ini";
    WriteToFile(invalid_ini, "path=/some/path");

    auto avds = Avd::List(paths_.avd_directory);
    EXPECT_EQ(avds.size(), 2);
    EXPECT_THAT(avds, ::testing::UnorderedElementsAre("avd1", "avd2"));
}

TEST_F(AvdTest, GetSysImg) {
    CreateTestAvd("q", "android-30", 30);

    auto expectedPath = paths_.sdk_directory / "sysimg" / "system.img";
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "q", false, ""));
    EXPECT_EQ(avd->GetSystemImagePaths().system_image, expectedPath);
}

TEST_F(AvdTest, SysImgOverride) {
    CreateTestAvd("q", "android-30", 30);

    tmp_->MakeSubDir("nothome");
    tmp_->MakeSubDir(fs::path("nothome") / "blah");
    auto overridePath = tmp_->Path() / "nothome" / "blah";

    // Create required files in override path
    WriteToFile(overridePath / "system.img", "some data");
    WriteToFile(overridePath / "build.prop",
                "ro.system.build.version.sdk=30\nro.product.cpu.abi=x86_64");
    WriteToFile(overridePath / "advancedFeatures.ini", "");
    WriteToFile(overridePath / "VerifiedBootParams.textproto", "");
    tmp_->MakeSubDir(fs::path("nothome") / "blah" / "data");
    WriteToFile(overridePath / "kernel_cmdline.txt", "");
    WriteToFile(overridePath / "kernel-ranchu", "");
    WriteToFile(overridePath / "ramdisk.img", "");
    WriteToFile(overridePath / "vendor.img", "");
    WriteToFile(overridePath / "encryptionkey.img", "");

    auto p = overridePath.string();
    opts_.sysdir = const_cast<char*>(p.c_str());

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "q", false, "", p));
    EXPECT_EQ(avd->GetSystemImagePaths().system_image, overridePath / "system.img");
}

TEST_F(AvdTest, WipeData) {
    auto avd_dir = CreateTestAvd("test_avd", "android-30", 30);

    auto some_file = avd_dir / "some-file";
    auto some_subdir = avd_dir / "some-subdir";
    auto some_subdir_file = avd_dir / "some-subdir" / "some-subdir-file";
    base::file::touch(some_file).IgnoreError();
    base::file::mkdir_recursive(some_subdir, 0755).IgnoreError();
    base::file::touch(some_subdir_file).IgnoreError();

    {
        ASSERT_OK_AND_ASSIGN(auto avd,
                             Avd::FromName(opts_, paths_, "test_avd", /*wipe_data=*/false, ""));
        EXPECT_TRUE(base::file::exists(some_file));
        EXPECT_TRUE(base::file::exists(some_subdir));
        EXPECT_TRUE(base::file::exists(some_subdir_file));
        EXPECT_TRUE(base::file::exists(avd_dir / "config.ini"));
    }

    {
        ASSERT_OK_AND_ASSIGN(auto avd,
                             Avd::FromName(opts_, paths_, "test_avd", /*wipe_data=*/true, ""));
        EXPECT_FALSE(base::file::exists(some_file));
        EXPECT_FALSE(base::file::exists(some_subdir));
        EXPECT_FALSE(base::file::exists(some_subdir_file));
        EXPECT_TRUE(base::file::exists(avd_dir / "config.ini"));
    }
}

TEST_F(AvdTest, FinalizeSavesConfig) {
    auto avd_dir = CreateTestAvd("test_avd", "android-30", 30);
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));

    auto hw_path = avd_dir / "hardware-qemu.ini";
    EXPECT_TRUE(base::file::exists(hw_path));

    auto hw_config = std::make_unique<IniFile>(hw_path);
    ASSERT_TRUE(hw_config->Read());
    EXPECT_EQ(hw_config->GetInt("hw.ramSize", 0), 2048);
    EXPECT_GE(hw_config->GetInt("vm.heapSize", 0), 16);
}

TEST_F(AvdTest, DisplayName) {
    auto avd_dir = CreateTestAvd("test_avd", "android-30", 30);
    WriteToFile(avd_dir / "config.ini",
                "avd.ini.displayname=My Custom AVD\ntarget=android-30\nimage.sysdir.1=sysimg");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->DisplayName(), "My Custom AVD");
}

TEST_F(AvdTest, SkinName) {
    auto avd_dir = CreateTestAvd("test_avd", "android-30", 30);
    WriteToFile(avd_dir / "config.ini",
                "skin.name=pixel_6\ntarget=android-30\nimage.sysdir.1=sysimg");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->SkinName(), "pixel_6");
}

TEST_F(AvdTest, CpuArchitecture) {
    auto avd_dir = CreateTestAvd("x86_avd", "android-30", 30);
    WriteToFile(avd_dir / "config.ini",
                "abi.type=x86_64\ntarget=android-30\nimage.sysdir.1=sysimg");
    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "x86_avd", false, ""));
        EXPECT_EQ(avd->Arch(), Avd::CpuArchitecture::kX86);
    }

    auto arm_avd_dir = CreateTestAvd("arm_avd", "android-30", 30);
    WriteToFile(arm_avd_dir / "config.ini",
                "abi.type=arm64-v8a\ntarget=android-30\nimage.sysdir.1=sysimg");
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.system.build.version.sdk=30\nro.product.cpu.abi=arm64-v8a");
    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "arm_avd", false, ""));
        EXPECT_EQ(avd->Arch(), Avd::CpuArchitecture::kArm);
    }

    auto unknown_avd_dir = CreateTestAvd("unknown_avd", "android-30", 30);
    WriteToFile(unknown_avd_dir / "config.ini",
                "abi.type=mips\ntarget=android-30\nimage.sysdir.1=sysimg");
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.system.build.version.sdk=30\nro.product.cpu.abi=mips");
    EXPECT_THAT(Avd::FromName(opts_, paths_, "unknown_avd", false, ""),
                StatusIs(absl::StatusCode::kInvalidArgument, "Unable to detect avd architecture"));
}

TEST_F(AvdTest, DeviceType) {
    auto avd_dir = CreateTestAvd("phone_avd", "android-30", 30);
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.product.name=sdk_gphone_x86_64\nro.system.build.version.sdk=30\nro.product.cpu."
                "abi=x86_64");
    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "phone_avd", false, ""));
        EXPECT_EQ(avd->GetDeviceType(), DeviceType::kPhone);
    }

    auto tv_avd_dir = CreateTestAvd("tv_avd", "android-30", 30);
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.product.name=sdk_atv_x86\nro.system.build.version.sdk=30\nro.product.cpu.abi="
                "x86_64");
    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "tv_avd", false, ""));
        EXPECT_EQ(avd->GetDeviceType(), DeviceType::kTv);
    }

    auto wear_avd_dir = CreateTestAvd("wear_avd", "android-30", 30);
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.product.name=sdk_wear_x86\nro.system.build.version.sdk=30\nro.product.cpu.abi="
                "x86_64");
    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "wear_avd", false, ""));
        EXPECT_EQ(avd->GetDeviceType(), DeviceType::kWear);
    }
}

TEST_F(AvdTest, QemuVersion) {
    CreateTestAvd("test_avd", "android-30", 30);
    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));

    // Initially no version
    EXPECT_THAT(avd->GetLastRunQemuVersion(), IsOkAndHolds(std::nullopt));

    // Set version
    ASSERT_OK(avd->SetLastRunQemuVersion(7));
    EXPECT_THAT(avd->GetLastRunQemuVersion(), IsOkAndHolds(7));

    // Overwrite version
    ASSERT_OK(avd->SetLastRunQemuVersion(8));
    EXPECT_THAT(avd->GetLastRunQemuVersion(), IsOkAndHolds(8));
}

TEST_F(AvdTest, BuildFingerprint) {
    CreateTestAvd("test_avd", "android-30", 30);
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.build.fingerprint=google/sdk_gphone_x86_64/emulator:30/RSR1.201013.001/"
                "6903271:userdebug/"
                "dev-keys\nro.system.build.version.sdk=30\nro.product.cpu.abi=x86_64");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "test_avd", false, ""));
    EXPECT_EQ(avd->BuildFingerprint(),
              "google/sdk_gphone_x86_64/emulator:30/RSR1.201013.001/6903271:userdebug/dev-keys");
}

TEST_F(AvdTest, Details) {
    auto avd_dir = CreateTestAvd("phone_avd", "android-30", 30);
    WriteToFile(avd_dir / "config.ini",
                "hw.lcd.width=1080\nhw.lcd.height=1920\ntarget=android-30\nimage.sysdir.1=sysimg");
    WriteToFile(paths_.sdk_directory / "sysimg" / "build.prop",
                "ro.product.name=sdk_gphone_x86_64\nro.system.build.version.sdk=30\nro.product.cpu."
                "abi=x86_64");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "phone_avd", false, ""));

    EXPECT_EQ(avd->Details(false), "phone_avd");

    auto verbose = avd->Details(true);
    EXPECT_THAT(verbose, ::testing::HasSubstr("phone_avd"));
    EXPECT_THAT(verbose, ::testing::HasSubstr("📱"));
    EXPECT_THAT(verbose, ::testing::HasSubstr("api: 30"));
    EXPECT_THAT(verbose, ::testing::HasSubstr("arch: x86_64"));
    EXPECT_THAT(verbose, ::testing::HasSubstr("res: 1080x1920"));
}

TEST_F(AvdTest, ContentOverride) {
    // Create original AVD with config.ini
    auto original_avd_dir = CreateTestAvd("override_test", "android-30", 30);
    WriteToFile(original_avd_dir / "config.ini",
                "hw.ramSize=1024\ntarget=android-30\nimage.sysdir.1=sysimg");

    // Create a content directory override in a different location.
    tmp_->MakeSubDir("unusual");
    fs::path override_path = tmp_->Path() / "unusual" / "my_avd.avd";
    base::file::mkdir_recursive(override_path, 0755).IgnoreError();

    ASSERT_OK_AND_ASSIGN(auto avd,
                         Avd::FromName(opts_, paths_, "override_test", false, override_path));

    // The content path should be the override path.
    EXPECT_EQ(avd->GetContentPath(), override_path);

    // Writable files (like hardware-qemu.ini) should be in the override path.
    EXPECT_TRUE(base::file::exists(override_path / "hardware-qemu.ini"));
    EXPECT_FALSE(base::file::exists(original_avd_dir / "hardware-qemu.ini"));

    // Hardware config should still be loaded from the original config.ini and finalized.
    EXPECT_EQ(avd->Hw().hw_ramSize, 2048);
}

TEST_F(AvdTest, FromAndroidBuild) {
    fs::path build_out = tmp_->Path() / "android_build_out";
    tmp_->MakeSubDir("android_build_out");
    tmp_->MakeSubDir("android_build_out/system");
    tmp_->MakeSubDir("android_build_out/data");

    WriteToFile(build_out / "system" / "build.prop",
                "ro.system.build.version.sdk=30\nro.product.cpu.abi=x86_64");
    WriteToFile(build_out / "advancedFeatures.ini", "");
    WriteToFile(build_out / "VerifiedBootParams.textproto", "");
    WriteToFile(build_out / "kernel_cmdline.txt", "");
    WriteToFile(build_out / "kernel-ranchu", "");
    WriteToFile(build_out / "ramdisk-qemu.img", "");
    WriteToFile(build_out / "system-qemu.img", "");
    WriteToFile(build_out / "vendor-qemu.img", "");
    WriteToFile(build_out / "encryptionkey.img", "");

    WriteToFile(build_out / "config.ini", "abi.type=x86_64\ntarget=android-30");

    ASSERT_OK_AND_ASSIGN(auto avd,
                         Avd::FromAndroidBuild(opts_, paths_, "android_build_avd", build_out,
                                               /*wipe_data=*/false, ""));

    EXPECT_EQ(avd->Name(), "android_build_avd");
    EXPECT_EQ(avd->Arch(), Avd::CpuArchitecture::kX86);
    EXPECT_EQ(avd->GetContentPath(), build_out);
}

TEST_F(AvdTest, FromAndroidBuildWipeData) {
    fs::path build_out = tmp_->Path() / "android_build_out_wipe";
    tmp_->MakeSubDir("android_build_out_wipe");
    tmp_->MakeSubDir("android_build_out_wipe/system");
    tmp_->MakeSubDir("android_build_out_wipe/data");

    WriteToFile(build_out / "system" / "build.prop",
                "ro.system.build.version.sdk=30\nro.product.cpu.abi=x86_64");
    WriteToFile(build_out / "advancedFeatures.ini", "");
    WriteToFile(build_out / "VerifiedBootParams.textproto", "");
    WriteToFile(build_out / "kernel_cmdline.txt", "");
    WriteToFile(build_out / "kernel-ranchu", "");
    WriteToFile(build_out / "ramdisk-qemu.img", "");
    WriteToFile(build_out / "system-qemu.img", "");
    WriteToFile(build_out / "vendor-qemu.img", "");
    WriteToFile(build_out / "encryptionkey.img", "");

    WriteToFile(build_out / "config.ini", "abi.type=x86_64\ntarget=android-30");

    auto qcow1 = build_out / "system.img.qcow2";
    auto qcow2 = build_out / "vendor.img.qcow2";
    auto qcow3 = build_out / "encryptionkey.img.qcow2";
    auto userdata = build_out / "userdata-qemu.img";
    auto userdata_qcow = build_out / "userdata-qemu.img.qcow2";
    auto cache = build_out / "cache.img.qcow2";
    auto hw_ini = build_out / "hardware-qemu.ini";
    auto qemu_ver = build_out / "qemu-version.txt";

    WriteToFile(qcow1, "dummy");
    WriteToFile(qcow2, "dummy");
    WriteToFile(qcow3, "dummy");
    WriteToFile(userdata, "dummy");
    WriteToFile(userdata_qcow, "dummy");
    WriteToFile(cache, "dummy");
    WriteToFile(hw_ini, "dummy");
    WriteToFile(qemu_ver, "dummy");

    EXPECT_TRUE(base::file::exists(qcow1));
    EXPECT_TRUE(base::file::exists(qcow2));
    EXPECT_TRUE(base::file::exists(qcow3));
    EXPECT_TRUE(base::file::exists(userdata));
    EXPECT_TRUE(base::file::exists(userdata_qcow));
    EXPECT_TRUE(base::file::exists(cache));
    EXPECT_TRUE(base::file::exists(hw_ini));
    EXPECT_TRUE(base::file::exists(qemu_ver));

    ASSERT_OK_AND_ASSIGN(auto avd,
                         Avd::FromAndroidBuild(opts_, paths_, "android_build_avd", build_out,
                                               /*wipe_data=*/true, ""));

    EXPECT_FALSE(base::file::exists(qcow1));
    EXPECT_FALSE(base::file::exists(qcow2));
    EXPECT_FALSE(base::file::exists(qcow3));
    EXPECT_FALSE(base::file::exists(userdata));
    EXPECT_FALSE(base::file::exists(userdata_qcow));
    EXPECT_FALSE(base::file::exists(cache));
    EXPECT_FALSE(base::file::exists(qemu_ver));
}

TEST_F(AvdTest, ImageKindPlayStore) {
    fs::path avd_dir = CreateTestAvd("playstore_avd", "android-30", 30);
    WriteToFile(avd_dir / "config.ini",
                "PlayStore.enabled=true\ntag.id=google_apis_playstore\nimage.sysdir.1=sysimg\n");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(opts_, paths_, "playstore_avd", false, {}, {}));
    EXPECT_EQ(avd->ImageKind(), android_studio::EmulatorAvdInfo::PLAY_STORE_KIND);
}

}  // namespace android::goldfish::avd
