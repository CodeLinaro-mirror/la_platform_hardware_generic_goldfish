// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/goldfish/input_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/log/log.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"

using android::base::TestSystem;
using android::base::TestTempDir;

namespace android::goldfish {

namespace fs = std::filesystem;

class InputPathsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        sys_ = std::make_unique<TestSystem>("/ignored", "/home");
        tmp_ = sys_->GetTempRoot();
        // TestSystem home is /home, but it's not created automatically in the temp root
        // as a subpath.
        // Actually, TestSystem redirects everything starting with / to its temp root.
        // But we must create the directory if we want exists() to pass.
    }

    void WriteToFile(const fs::path& path, std::string_view text) {
        base::file::mkdir_recursive(path.parent_path(), 0755).IgnoreError();
        std::ofstream file(path, std::ios::trunc);
        file << text;
    }

    std::unique_ptr<TestSystem> sys_;
    TestTempDir* tmp_;
};

TEST_F(InputPathsTest, ResolveUserPaths) {
    fs::path user_dir = tmp_->Path() / "user_home";
    fs::path avd_root = tmp_->Path() / "avd_root";
    fs::path sdk_root = tmp_->Path() / "sdk_root";
    fs::path runtime_dir = tmp_->Path() / "runtime";

    tmp_->MakeSubDir("user_home");
    tmp_->MakeSubDir("avd_root");
    tmp_->MakeSubDir("avd_root/avd");
    tmp_->MakeSubDir("sdk_root");
    tmp_->MakeSubDir("sdk_root/platforms");
    tmp_->MakeSubDir("sdk_root/platform-tools");
    tmp_->MakeSubDir("runtime");

    sys_->EnvSet("ANDROID_EMULATOR_HOME", user_dir.string());
    sys_->EnvSet("ANDROID_AVD_HOME", avd_root.string());
    sys_->EnvSet("ANDROID_HOME", sdk_root.string());
    sys_->EnvSet("XDG_RUNTIME_DIR", runtime_dir.string());

    ASSERT_OK_AND_ASSIGN(auto paths, ResolveUserPaths(tmp_->Path(), false));

    EXPECT_EQ(paths.user_directory, user_dir);
    EXPECT_EQ(paths.avd_directory, avd_root);
    EXPECT_EQ(paths.sdk_directory, sdk_root);
#ifdef __linux__
    EXPECT_EQ(paths.discovery_directory, runtime_dir / "avd" / "running");
#else
    EXPECT_EQ(paths.discovery_directory, user_dir / "avd" / "running");
#endif
}

TEST_F(InputPathsTest, ResolveUserPathsWithoutSdkRoot) {
    fs::path user_dir = tmp_->Path() / "user_home";
    fs::path avd_root = tmp_->Path() / "avd_root";
    fs::path runtime_dir = tmp_->Path() / "runtime";

    tmp_->MakeSubDir("user_home");
    tmp_->MakeSubDir("avd_root");
    tmp_->MakeSubDir("avd_root/avd");
    tmp_->MakeSubDir("runtime");

    sys_->EnvSet("ANDROID_EMULATOR_HOME", user_dir.string());
    sys_->EnvSet("ANDROID_AVD_HOME", avd_root.string());
    sys_->EnvSet("XDG_RUNTIME_DIR", runtime_dir.string());

    ASSERT_OK_AND_ASSIGN(auto paths, ResolveUserPaths(tmp_->Path(), false));

    EXPECT_EQ(paths.user_directory, user_dir);
    EXPECT_EQ(paths.avd_directory, avd_root);
    EXPECT_EQ(paths.sdk_directory, fs::path());
#ifdef __linux__
    EXPECT_EQ(paths.discovery_directory, runtime_dir / "avd" / "running");
#else
    EXPECT_EQ(paths.discovery_directory, user_dir / "avd" / "running");
#endif
}

TEST_F(InputPathsTest, ResolveSystemImagePaths) {
    fs::path sysimg_dir = tmp_->Path() / "sysimg";
    tmp_->MakeSubDir("sysimg");
    tmp_->MakeSubDir("sysimg/data");

    WriteToFile(sysimg_dir / "build.prop", "");
    WriteToFile(sysimg_dir / "advancedFeatures.ini", "");
    WriteToFile(sysimg_dir / "VerifiedBootParams.textproto", "");
    WriteToFile(sysimg_dir / "kernel_cmdline.txt", "");
    WriteToFile(sysimg_dir / "kernel-ranchu", "");
    WriteToFile(sysimg_dir / "ramdisk.img", "");
    WriteToFile(sysimg_dir / "system.img", "");
    WriteToFile(sysimg_dir / "vendor.img", "");
    WriteToFile(sysimg_dir / "encryptionkey.img", "");

    AndroidOptions opts = {};
    ASSERT_OK_AND_ASSIGN(auto paths, ResolveSystemImagePaths({sysimg_dir}, opts, /*android_build=*/false));

    EXPECT_EQ(paths.build_properties, sysimg_dir / "build.prop");
    EXPECT_EQ(paths.system_image, sysimg_dir / "system.img");
    EXPECT_EQ(paths.data_dir, sysimg_dir / "data");
}

TEST_F(InputPathsTest, ResolveSystemImagePathsAndroidBuild) {
    fs::path sysimg_dir = tmp_->Path() / "sysimg";
    tmp_->MakeSubDir("sysimg");
    tmp_->MakeSubDir("sysimg/data");

    WriteToFile(sysimg_dir / "build.prop", "");
    WriteToFile(sysimg_dir / "advancedFeatures.ini", "");
    WriteToFile(sysimg_dir / "VerifiedBootParams.textproto", "");
    WriteToFile(sysimg_dir / "kernel_cmdline.txt", "");
    WriteToFile(sysimg_dir / "kernel-ranchu", "");
    WriteToFile(sysimg_dir / "ramdisk-qemu.img", "");
    WriteToFile(sysimg_dir / "system-qemu.img", "");
    WriteToFile(sysimg_dir / "vendor-qemu.img", "");
    WriteToFile(sysimg_dir / "encryptionkey.img", "");

    AndroidOptions opts = {};
    ASSERT_OK_AND_ASSIGN(auto paths, ResolveSystemImagePaths({sysimg_dir}, opts, /*android_build=*/true));

    EXPECT_EQ(paths.build_properties, sysimg_dir / "build.prop");
    EXPECT_EQ(paths.ramdisk_image, sysimg_dir / "ramdisk-qemu.img");
    EXPECT_EQ(paths.system_image, sysimg_dir / "system-qemu.img");
    EXPECT_EQ(paths.vendor_image, sysimg_dir / "vendor-qemu.img");
    EXPECT_EQ(paths.data_dir, sysimg_dir / "data");
}

TEST_F(InputPathsTest, ResolveSystemImagePathsWithOverrides) {
    fs::path sysimg_dir = tmp_->Path() / "sysimg";
    tmp_->MakeSubDir("sysimg");
    tmp_->MakeSubDir("sysimg/data");

    WriteToFile(sysimg_dir / "build.prop", "");
    WriteToFile(sysimg_dir / "advancedFeatures.ini", "");
    WriteToFile(sysimg_dir / "VerifiedBootParams.textproto", "");
    WriteToFile(sysimg_dir / "kernel_cmdline.txt", "");
    WriteToFile(sysimg_dir / "kernel-ranchu", "");
    WriteToFile(sysimg_dir / "ramdisk.img", "");
    WriteToFile(sysimg_dir / "system.img", "");
    WriteToFile(sysimg_dir / "vendor.img", "");
    WriteToFile(sysimg_dir / "encryptionkey.img", "");

    fs::path override_kernel = tmp_->Path() / "custom-kernel";
    WriteToFile(override_kernel, "kernel data");
    std::string override_kernel_str = override_kernel.string();

    fs::path override_ramdisk = tmp_->Path() / "custom-ramdisk";
    WriteToFile(override_ramdisk, "ramdisk data");
    std::string override_ramdisk_str = override_ramdisk.string();

    fs::path override_system = tmp_->Path() / "custom-system";
    WriteToFile(override_system, "system data");
    std::string override_system_str = override_system.string();

    fs::path override_vendor = tmp_->Path() / "custom-vendor";
    WriteToFile(override_vendor, "vendor data");
    std::string override_vendor_str = override_vendor.string();

    fs::path override_encryption_key = tmp_->Path() / "custom-encryption_key";
    WriteToFile(override_encryption_key, "encryption_key data");
    std::string override_encryption_key_str = override_encryption_key.string();

    AndroidOptions opts = {};
    opts.kernel = const_cast<char*>(override_kernel_str.c_str());
    opts.ramdisk = const_cast<char*>(override_ramdisk_str.c_str());
    opts.system = const_cast<char*>(override_system_str.c_str());
    opts.vendor = const_cast<char*>(override_vendor_str.c_str());
    opts.encryption_key = const_cast<char*>(override_encryption_key_str.c_str());

    ASSERT_OK_AND_ASSIGN(auto paths, ResolveSystemImagePaths({sysimg_dir}, opts, /*android_build=*/false));

    EXPECT_EQ(paths.kernel_image, override_kernel);
    EXPECT_EQ(paths.ramdisk_image, override_ramdisk);
    EXPECT_EQ(paths.system_image, override_system);
    EXPECT_EQ(paths.vendor_image, override_vendor);
    EXPECT_EQ(paths.encryption_key_image, override_encryption_key);
}

TEST_F(InputPathsTest, ResolveEmulatorPaths) {
    tmp_->MakeSubDir("launcher");
    tmp_->MakeSubDir("launcher/bin");
    tmp_->MakeSubDir("launcher/lib/qemu");
    tmp_->MakeSubDir("launcher/lib64");
    tmp_->MakeSubDir("launcher/share/qemu");

    fs::path launcher_dir = tmp_->Path() / "launcher";
    // For Windows, canonicalize.
    ASSERT_OK_AND_ASSIGN(auto canon, android::base::file::make_canonical(launcher_dir));
    launcher_dir = canon;

    std::string suffix = "";
#ifdef _WIN32
    suffix = ".exe";
#endif

    WriteToFile(launcher_dir / "bin" / ("qemu-system-x86_64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-system-aarch64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-img" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("netsimd" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("crashpad_handler" + suffix), "");

    sys_->SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR", launcher_dir.string());

    // ResolveEmulatorPaths will use the current binary path as launcher_binary,
    // but should use ANDROID_EMULATOR_LAUNCHER_DIR for other paths.
    ASSERT_OK_AND_ASSIGN(auto paths, ResolveEmulatorPaths(false));

    EXPECT_EQ(paths.launcher_directory, launcher_dir);
    EXPECT_EQ(paths.binary_directory, launcher_dir / "bin");
    EXPECT_EQ(paths.qemu_system_x86_binary, launcher_dir / "bin" / ("qemu-system-x86_64" + suffix));
    EXPECT_FALSE(paths.HasFishtank());
}

TEST_F(InputPathsTest, ResolveEmulatorPathsWithFishtank) {
    tmp_->MakeSubDir("launcher");
    tmp_->MakeSubDir("launcher/bin");
    tmp_->MakeSubDir("launcher/lib/qemu");
    tmp_->MakeSubDir("launcher/lib64");
    tmp_->MakeSubDir("launcher/share/qemu");
    tmp_->MakeSubDir("launcher/fishtank");

    fs::path launcher_dir = tmp_->Path() / "launcher";
    ASSERT_OK_AND_ASSIGN(auto canon, android::base::file::make_canonical(launcher_dir));
    launcher_dir = canon;

    std::string suffix = "";
#ifdef _WIN32
    suffix = ".exe";
#endif

    WriteToFile(launcher_dir / "bin" / ("qemu-system-x86_64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-system-aarch64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-img" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("netsimd" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("crashpad_handler" + suffix), "");
    WriteToFile(launcher_dir / "fishtank" / ("fishtank" + suffix), "mock_executable_bytes");

    sys_->SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR", launcher_dir.string());

    ASSERT_OK_AND_ASSIGN(auto paths, ResolveEmulatorPaths(false));

    EXPECT_TRUE(paths.HasFishtank());
    EXPECT_EQ(paths.fishtank_binary, launcher_dir / "fishtank" / ("fishtank" + suffix));
}

TEST_F(InputPathsTest, ResolveEmulatorPathsWithZeroByteFishtank) {
    tmp_->MakeSubDir("launcher");
    tmp_->MakeSubDir("launcher/bin");
    tmp_->MakeSubDir("launcher/lib/qemu");
    tmp_->MakeSubDir("launcher/lib64");
    tmp_->MakeSubDir("launcher/share/qemu");
    tmp_->MakeSubDir("launcher/fishtank");

    fs::path launcher_dir = tmp_->Path() / "launcher";
    ASSERT_OK_AND_ASSIGN(auto canon, android::base::file::make_canonical(launcher_dir));
    launcher_dir = canon;

    std::string suffix = "";
#ifdef _WIN32
    suffix = ".exe";
#endif

    WriteToFile(launcher_dir / "bin" / ("qemu-system-x86_64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-system-aarch64" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("qemu-img" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("netsimd" + suffix), "");
    WriteToFile(launcher_dir / "bin" / ("crashpad_handler" + suffix), "");
    // Zero-byte placeholder as created by AOSP empty.zip
    WriteToFile(launcher_dir / "fishtank" / ("fishtank" + suffix), "");

    sys_->SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR", launcher_dir.string());

    ASSERT_OK_AND_ASSIGN(auto paths, ResolveEmulatorPaths(false));

    EXPECT_FALSE(paths.HasFishtank());
    EXPECT_TRUE(paths.fishtank_binary.empty());
}

}  // namespace android::goldfish
