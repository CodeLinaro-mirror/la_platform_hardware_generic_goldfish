// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "initrd_device.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "android/base/testing/needs_winsock.h"
#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/sockets/scoped_socket.h"
#include "android/sockets/socket_utils.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"
#include "goldfish/adb/adbkey.h"
#include "goldfish/adb/testing/adbkey_test_support.h"
#include "goldfish/file/file.h"
#include "snapshot_device.h"

namespace android::goldfish::test {
namespace {

using ::goldfish::adb::testing::CreateStubAdbExecutable;
using ::goldfish::adb::testing::GenerateTestAdbPrivateKey;
using ::goldfish::adb::testing::ScopedFakeAdbKeygen;

class MockAdbKillServer {
  public:
    MockAdbKillServer() : listen_socket_(android::base::socketTcp4LoopbackServer(0)) {
        if (listen_socket_.valid()) {
            port_ = android::base::socketGetPort(listen_socket_.get());
            thread_ = std::thread([this]() { Run(); });
        }
    }

    ~MockAdbKillServer() { StopAndJoin(); }

    MockAdbKillServer(const MockAdbKillServer&) = delete;
    MockAdbKillServer& operator=(const MockAdbKillServer&) = delete;

    bool Valid() const { return listen_socket_.valid() && port_ > 0; }
    int Port() const { return port_; }
    int ConnectionCount() const { return connection_count_.load(); }

    std::string ReceivedRequest() {
        StopAndJoin();
        return received_;
    }

    void StopAndJoin() {
        if (!thread_.joinable()) {
            return;
        }
        shutting_down_.store(true);
        if (!handled_client_.load()) {
            android::base::ScopedSocket wake(android::base::socketTcp4LoopbackClient(port_));
        }
        thread_.join();
    }

  private:
    static constexpr std::string_view kExpectedKillWire = "0009host:kill";

    void Run() {
        const int client_fd = android::base::socketAcceptAny(listen_socket_.get());
        if (client_fd < 0) {
            return;
        }
        if (shutting_down_.load()) {
            android::base::socketClose(client_fd);
            return;
        }
        handled_client_.store(true);
        connection_count_.fetch_add(1);
        received_.resize(kExpectedKillWire.size());
        if (android::base::socketRecvAll(client_fd, received_.data(), received_.size())) {
            android::base::socketSendAll(client_fd, "OKAY", 4);
        }
        android::base::socketClose(client_fd);
    }

    android::base::ScopedSocket listen_socket_;
    int port_ = -1;
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> handled_client_{false};
    std::atomic<int> connection_count_{0};
    std::string received_;
    std::thread thread_;
};

class ScopedWarningLogSink : public absl::LogSink {
  public:
    ScopedWarningLogSink() { absl::AddLogSink(this); }
    ~ScopedWarningLogSink() override { absl::RemoveLogSink(this); }

    ScopedWarningLogSink(const ScopedWarningLogSink&) = delete;
    ScopedWarningLogSink& operator=(const ScopedWarningLogSink&) = delete;

    void Send(const absl::LogEntry& entry) override {
        if (entry.log_severity() == absl::LogSeverity::kWarning) {
            warnings_.emplace_back(entry.text_message());
        }
    }

    const std::vector<std::string>& warnings() const { return warnings_; }

  private:
    std::vector<std::string> warnings_;
};

}  // namespace

TEST(BootProperties, Basic) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();

    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.hardware", "ranchu"},
                           std::pair{"androidboot.logcat", "*:V"},
                           std::pair{"androidboot.debug.hwui.renderer", "skiavk"},
                           std::pair{"androidboot.debug.renderengine.backend", "skiavkthreaded"},
                       }));
    EXPECT_THAT(props,
                testing::Not(testing::Contains(testing::Pair(
                        "androidboot.surface_flinger.renderengine_graphite_vulkan", testing::_))));
}

TEST(BootProperties, NoBootAnim) {
    base::TestSystem sys("");

    AndroidOptions opts{.no_boot_anim = true};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(getBootProperties(emu.config(), "lavapipe"),
                testing::IsSupersetOf(std::vector{std::pair{"android.bootanim", "0"}}));
}

TEST(BootProperties, Logcat) {
    base::TestSystem sys("");

    AndroidOptions opts{.logcat = "*:S Zygote:E"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(
            getBootProperties(emu.config(), "lavapipe"),
            testing::IsSupersetOf(std::vector{std::pair{"androidboot.logcat", "*:S,Zygote:E"}}));
}

TEST(BootProperties, VerifiedBootParams) {
    base::TestSystem sys("");
    auto temp_path = sys.GetTempRoot()->Path();
    auto params_path = temp_path / "VerifiedBootParams.textproto";
    {
        std::ofstream out(params_path);
        out << "# Verified boot params\n"
            << "major_version: 2\n"
            << "param: \"androidboot.vbmeta.size=6720\"\n"
            << "param: \"androidboot.vbmeta.hash_alg=sha256\"\n"
            << "param: \"androidboot.vbmeta.digest=abc123\"\n";
    }

    FakeEmulator emu;
    emu.system_image_paths().verified_boot_params = params_path;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.vbmeta.size", "6720"},
                           std::pair{"androidboot.vbmeta.hash_alg", "sha256"},
                           std::pair{"androidboot.vbmeta.digest", "abc123"},
                       }));
    EXPECT_THAT(
            props,
            testing::Not(testing::Contains(std::pair{"androidboot.verifiedbootstate", "orange"})));
}

TEST(BootProperties, WritableSystemSetsOrangeState) {
    base::TestSystem sys("");

    AndroidOptions opts{.writable_system = true};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_THAT(getBootProperties(emu.config(), "lavapipe"),
                testing::IsSupersetOf(
                        std::vector{std::pair{"androidboot.verifiedbootstate", "orange"}}));
}

TEST(BootProperties, EmptyVerifiedBootParams) {
    base::TestSystem sys("");
    auto temp_path = sys.GetTempRoot()->Path();
    auto params_path = temp_path / "VerifiedBootParams.textproto";
    std::ofstream{params_path};

    FakeEmulator emu;
    emu.system_image_paths().verified_boot_params = params_path;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::Not(testing::Contains(
                               testing::Key(testing::StartsWith("androidboot.vbmeta")))));
}

TEST(BootProperties, VerifiedBootParamsAllowsUnknownFields) {
    base::TestSystem sys("");
    auto temp_path = sys.GetTempRoot()->Path();
    auto params_path = temp_path / "VerifiedBootParams.textproto";
    {
        std::ofstream out(params_path);
        out << "major_version: 2\n"
            << "future_metadata_field: \"some_value\"\n"
            << "param: \"androidboot.vbmeta.size=6720\"\n";
    }

    FakeEmulator emu;
    emu.system_image_paths().verified_boot_params = params_path;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props,
                testing::IsSupersetOf(std::vector{std::pair{"androidboot.vbmeta.size", "6720"}}));
}

TEST(BootProperties, UnsupportedVerifiedBootMajorVersion) {
    base::TestSystem sys("");
    auto temp_path = sys.GetTempRoot()->Path();
    auto params_path = temp_path / "VerifiedBootParams.textproto";
    {
        std::ofstream out(params_path);
        out << "major_version: 3\n"
            << "param: \"androidboot.vbmeta.size=6720\"\n";
    }

    FakeEmulator emu;
    emu.system_image_paths().verified_boot_params = params_path;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props,
                testing::Not(testing::Contains(std::pair{"androidboot.vbmeta.size", "6720"})));
}

TEST(Initrd, Basic) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();

    android::base::file::mkdir_recursive(launcher_path / "content", 0755).IgnoreError();
    android::base::file::mkdir_recursive(launcher_path / "system", 0755).IgnoreError();

    auto system_initrd = launcher_path / "system/ramdisk.img";
    std::ofstream{system_initrd};

    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));

    SystemImagePaths paths;
    paths.ramdisk_image = system_initrd;
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .WillRepeatedly(testing::Return((launcher_path / "content").string()));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    EXPECT_OK(gpu_dev.initialize(emu.config()));
    InitrdDevice dev(gpu_dev, snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-initrd"),
                        testing::EndsWith(fs::path("content/initrd").make_preferred().string())));
}

TEST(Initrd, RamdiskFlag) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();

    android::base::file::mkdir_recursive(launcher_path / "content", 0755).IgnoreError();
    android::base::file::mkdir_recursive(launcher_path / "system", 0755).IgnoreError();

    auto system_initrd = launcher_path / "system/ramdisk.img";
    std::ofstream{system_initrd};

    auto override_initrd = launcher_path / "system/other-initrd";
    std::ofstream{override_initrd} << "abc";

    char override_str[1024];
    strncpy(override_str, override_initrd.string().c_str(), 1024);
    AndroidOptions opts{.ramdisk = override_str};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).Times(1).WillRepeatedly(testing::Return("mock_avd"));

    SystemImagePaths paths;
    paths.ramdisk_image = override_initrd;
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .WillRepeatedly(testing::Return((launcher_path / "content").string()));
    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    EXPECT_OK(gpu_dev.initialize(emu.config()));
    InitrdDevice dev(gpu_dev, snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    auto params = dev.getQemuParameters(emu.config());

    EXPECT_THAT(params,
                testing::ElementsAre(
                        testing::Eq("-initrd"),
                        testing::EndsWith(fs::path("content/initrd").make_preferred().string())));
    std::string contents;
    std::ifstream{params[1]} >> contents;
    EXPECT_THAT(contents, testing::StartsWith("abc"));
}

TEST(Initrd, WarnsWhenChangedOnSnapshotLoad) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();
    auto content_path = launcher_path / "content";
    auto snapshot_dir = content_path / "snapshots" / "default_boot";

    ASSERT_OK(android::base::file::mkdir_recursive(snapshot_dir, 0755));
    ASSERT_OK(android::base::file::mkdir_recursive(launcher_path / "system", 0755));
    {
        std::ofstream out(snapshot_dir / "version.txt");
        out << kSnapshotVersion;
    }

    auto system_initrd = launcher_path / "system" / "ramdisk.img";
    std::ofstream{system_initrd} << "ramdisk-bytes";

    auto hw = HardwareConfig();
    SystemImagePaths paths;
    paths.ramdisk_image = system_initrd;

    // First run creates content/initrd.
    {
        FakeEmulator emu;
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
        EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths())
                .WillRepeatedly(testing::ReturnRef(paths));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
        EXPECT_CALL(emu.mock_avd(), Arch())
                .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

        SnapshotDevice snapshot_dev;
        ASSERT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice gpu_dev("gpu0", snapshot_dev);
        ASSERT_OK(gpu_dev.initialize(emu.config()));
        InitrdDevice dev(gpu_dev, snapshot_dev);
        ASSERT_OK(dev.initialize(emu.config()));
    }

    // Second run loads default_boot with changed boot properties.
    FakeEmulator emu(AndroidOptions{.no_boot_anim = true});
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_EQ(snapshot_dev.load_status(), SnapshotDevice::LoadStatus::kLoading);
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    ASSERT_OK(gpu_dev.initialize(emu.config()));
    InitrdDevice dev(gpu_dev, snapshot_dev);

    ScopedWarningLogSink log_sink;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(log_sink.warnings(),
                testing::Contains(testing::HasSubstr(
                        "Initrd configuration has changed while loading snapshot 'default_boot'")));

    auto updated_initrd =
            android::base::file::read_whole_file(content_path / "initrd", /*binary=*/true);
    ASSERT_OK(updated_initrd);
    EXPECT_THAT(*updated_initrd, testing::HasSubstr("android.bootanim=\"0\""));
}

TEST(Initrd, DoesNotWarnWhenUnchangedOnSnapshotLoad) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();
    auto content_path = launcher_path / "content";
    auto snapshot_dir = content_path / "snapshots" / "default_boot";

    ASSERT_OK(android::base::file::mkdir_recursive(snapshot_dir, 0755));
    ASSERT_OK(android::base::file::mkdir_recursive(launcher_path / "system", 0755));
    {
        std::ofstream out(snapshot_dir / "version.txt");
        out << kSnapshotVersion;
    }

    auto system_initrd = launcher_path / "system" / "ramdisk.img";
    std::ofstream{system_initrd} << "ramdisk-bytes";

    auto hw = HardwareConfig();
    SystemImagePaths paths;
    paths.ramdisk_image = system_initrd;

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_EQ(snapshot_dev.load_status(), SnapshotDevice::LoadStatus::kLoading);
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    ASSERT_OK(gpu_dev.initialize(emu.config()));

    InitrdDevice first_dev(gpu_dev, snapshot_dev);
    ASSERT_OK(first_dev.initialize(emu.config()));

    ScopedWarningLogSink log_sink;
    InitrdDevice second_dev(gpu_dev, snapshot_dev);
    EXPECT_OK(second_dev.initialize(emu.config()));
    EXPECT_THAT(log_sink.warnings(),
                testing::Not(testing::Contains(testing::HasSubstr(
                        "Initrd configuration has changed while loading snapshot"))));
}

TEST(Initrd, DoesNotWarnWhenChangedOnColdBoot) {
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();
    auto content_path = launcher_path / "content";

    ASSERT_OK(android::base::file::mkdir_recursive(content_path, 0755));
    ASSERT_OK(android::base::file::mkdir_recursive(launcher_path / "system", 0755));

    auto system_initrd = launcher_path / "system" / "ramdisk.img";
    std::ofstream{system_initrd} << "ramdisk-bytes";

    auto hw = HardwareConfig();
    SystemImagePaths paths;
    paths.ramdisk_image = system_initrd;

    {
        FakeEmulator emu(AndroidOptions{.no_snapshot_load = true});
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
        EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths())
                .WillRepeatedly(testing::ReturnRef(paths));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
        EXPECT_CALL(emu.mock_avd(), Arch())
                .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

        SnapshotDevice snapshot_dev;
        ASSERT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice gpu_dev("gpu0", snapshot_dev);
        ASSERT_OK(gpu_dev.initialize(emu.config()));
        InitrdDevice dev(gpu_dev, snapshot_dev);
        ASSERT_OK(dev.initialize(emu.config()));
    }

    FakeEmulator emu(AndroidOptions{.no_boot_anim = true, .no_snapshot_load = true});
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_NE(snapshot_dev.load_status(), SnapshotDevice::LoadStatus::kLoading);
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    ASSERT_OK(gpu_dev.initialize(emu.config()));
    InitrdDevice dev(gpu_dev, snapshot_dev);

    ScopedWarningLogSink log_sink;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(log_sink.warnings(),
                testing::Not(testing::Contains(testing::HasSubstr(
                        "Initrd configuration has changed while loading snapshot"))));
}

TEST(BootProperties, GuestAngleFeatureFlag) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");

    // Default FakeEmulator has GuestAngle enabled.
    {
        FakeEmulator emu;
        auto hw = HardwareConfig();
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
        EXPECT_CALL(emu.mock_avd(), Arch())
                .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

        const auto props = getBootProperties(emu.config(), "lavapipe");
        EXPECT_THAT(props, testing::Contains(std::pair{"androidboot.hardwareegl", "angle"}));
    }

    // Disabling GuestAngle omits the androidboot.hardwareegl boot property.
    {
        FakeEmulator emu;
        emu.feature_flags().SetEnabledOverride(S::GUEST_ANGLE, false);
        auto hw = HardwareConfig();
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
        EXPECT_CALL(emu.mock_avd(), Arch())
                .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

        const auto props = getBootProperties(emu.config(), "lavapipe");
        EXPECT_THAT(props, testing::Not(testing::Contains(
                                   testing::Pair("androidboot.hardwareegl", testing::_))));
    }
}

TEST(Initrd, FailsWhenMinigbmDisabled) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.feature_flags().SetEnabledOverride(S::MINIGBM, false);

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    EXPECT_OK(gpu_dev.initialize(emu.config()));

    InitrdDevice dev(gpu_dev, snapshot_dev);
    auto status = dev.initialize(emu.config());
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Initrd, SucceedsBelowApi37WhenMinigbmDisabled) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    auto launcher_path = sys.GetTempRoot()->Path();
    ASSERT_OK(android::base::file::mkdir_recursive(launcher_path / "content", 0755));
    ASSERT_OK(android::base::file::mkdir_recursive(launcher_path / "system", 0755));
    auto system_initrd = launcher_path / "system/ramdisk.img";
    std::ofstream{system_initrd};

    FakeEmulator emu;
    emu.feature_flags().SetEnabledOverride(S::MINIGBM, false);

    auto hw = HardwareConfig();
    SystemImagePaths paths;
    paths.ramdisk_image = system_initrd;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), GetContentPath())
            .WillRepeatedly(testing::Return((launcher_path / "content").string()));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    EXPECT_OK(gpu_dev.initialize(emu.config()));

    InitrdDevice dev(gpu_dev, snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
}

TEST(BootProperties, LegacyGraphicsPropertiesBelowApi37) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.feature_flags().SetEnabledOverride(S::MINIGBM, false);

    auto hw = HardwareConfig();
    hw.hw_gltransport = "virtio-gpu-pipe";
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.qemu.gltransport.name", "asg"},
                           std::pair{"androidboot.hardware.gltransport", "asg"},
                           std::pair{"androidboot.qemu.gltransport.drawFlushInterval", "800"},
                           std::pair{"androidboot.opengles.version", "196609"},
                           std::pair{"androidboot.hardware.vulkan", "ranchu"},
                           std::pair{"androidboot.debug.hwui.renderer", "skiagl"},
                           std::pair{"androidboot.debug.renderengine.backend", "skiaglthreaded"},
                       }));
    EXPECT_THAT(props, testing::Not(testing::Contains(
                               testing::Pair("androidboot.hardware.gralloc", testing::_))));
    EXPECT_THAT(
            props,
            testing::Not(testing::Contains(testing::Pair("androidboot.hardwareegl", testing::_))));

    // API 31 (minimum legacy Vulkan API level) still advertises ranchu Vulkan.
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(31));
    EXPECT_THAT(getBootProperties(emu.config(), "lavapipe"),
                testing::Contains(std::pair{"androidboot.hardware.vulkan", "ranchu"}));

    // API 30 and below do not enable Vulkan on goldfish_pipe.
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(30));
    EXPECT_THAT(getBootProperties(emu.config(), "lavapipe"),
                testing::Not(testing::Contains(
                        testing::Pair("androidboot.hardware.vulkan", testing::_))));

    // Explicit -renderer-features Vulkan:disabled suppresses ranchu Vulkan on API 31..36.
    char vk_disabled[] = "Vulkan:disabled";
    AndroidOptions disabled_opts{.renderer_features = vk_disabled};
    FakeEmulator disabled_emu(std::move(disabled_opts));
    disabled_emu.feature_flags().SetEnabledOverride(S::MINIGBM, false);
    EXPECT_CALL(disabled_emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(disabled_emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(disabled_emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(disabled_emu.mock_avd(), Arch())
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));
    EXPECT_THAT(getBootProperties(disabled_emu.config(), "lavapipe"),
                testing::Not(testing::Contains(
                        testing::Pair("androidboot.hardware.vulkan", testing::_))));
}

TEST(BootProperties, ModernGraphicsPropertiesBelowApi37WithMinigbmFeature) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.feature_flags().SetEnabledOverride(S::MINIGBM, true);
    emu.feature_flags().SetEnabledOverride(S::GUEST_ANGLE, false);

    auto hw = HardwareConfig();
    hw.hw_gltransport = "virtio-gpu-pipe";
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.hardware.gralloc", "minigbm"},
                           std::pair{"androidboot.qemu.gltransport.name", "virtio-gpu-pipe"},
                           std::pair{"androidboot.hardware.gltransport", "virtio-gpu-pipe"},
                           std::pair{"androidboot.qemu.gltransport.drawFlushInterval", "800"},
                           std::pair{"androidboot.opengles.version", "196610"},
                           std::pair{"androidboot.hardware.vulkan", "ranchu"},
                           std::pair{"androidboot.debug.hwui.renderer", "skiavk"},
                           std::pair{"androidboot.debug.renderengine.backend", "skiavkthreaded"},
                       }));
    EXPECT_THAT(
            props,
            testing::Not(testing::Contains(testing::Pair("androidboot.hardwareegl", testing::_))));
}

TEST(BootProperties, SystemUiRendererGraphiteOnApi37Plus) {
    base::TestSystem sys("");
    char renderer[] = "graphite";
    AndroidOptions opts{.systemui_renderer = renderer};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props,
                testing::IsSupersetOf(std::vector{
                    std::pair{"androidboot.debug.hwui.renderer", "skiavk"},
                    std::pair{"androidboot.debug.renderengine.backend", "skiavkthreaded"},
                    std::pair{"androidboot.surface_flinger.renderengine_graphite_vulkan", "true"},
                }));
}

TEST(BootProperties, SystemUiRendererGraphiteBelowApi37UsesPreviousDefaults) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    char renderer[] = "graphite";
    AndroidOptions opts{.systemui_renderer = renderer};
    FakeEmulator emu(std::move(opts));
    emu.feature_flags().SetEnabledOverride(S::MINIGBM, false);

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.debug.hwui.renderer", "skiagl"},
                           std::pair{"androidboot.debug.renderengine.backend", "skiaglthreaded"},
                       }));
    EXPECT_THAT(props,
                testing::Not(testing::Contains(testing::Pair(
                        "androidboot.surface_flinger.renderengine_graphite_vulkan", testing::_))));
}

TEST(BootPropertiesDeathTest, FailsBelowMinSupportedApiLevel) {
    base::TestSystem sys("");
    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(23));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    EXPECT_DEATH(getBootProperties(emu.config(), "lavapipe"),
                 "API levels below 24 are not supported yet\\.");
}

TEST(Initrd, FailsWhenDynamicPartitionDisabled) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.feature_flags().SetEnabledOverride(S::DYNAMIC_PARTITION, false);

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    EXPECT_OK(gpu_dev.initialize(emu.config()));

    InitrdDevice dev(gpu_dev, snapshot_dev);
    auto status = dev.initialize(emu.config());
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(BootProperties, FoldableProperties) {
    base::TestSystem sys("");
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_sensor_hinge = true;
    hw.hw_sensor_hinge_count = 1;
    hw.hw_displayRegion_0_1_width = 1080;
    hw.hw_displayRegion_0_1_height = 2092;
    hw.hw_lcd_density = 420;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::IsSupersetOf(std::vector{
                           std::pair{"androidboot.qemu.external.displays", "1,1080,2092,420,0"},
                           std::pair{"androidboot.qemu.autorotate", "1"},
                       }));
}

TEST(BootProperties, FoldableWithoutSecondaryDisplayRegionOmitsExternalDisplays) {
    base::TestSystem sys("");
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_sensor_hinge = true;
    hw.hw_sensor_hinge_count = 1;
    hw.hw_displayRegion_0_1_width = 0;
    hw.hw_displayRegion_0_1_height = 0;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(36));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(props, testing::Contains(std::pair{"androidboot.qemu.autorotate", "1"}));
    EXPECT_THAT(props, testing::Not(testing::Contains(
                               testing::Pair("androidboot.qemu.external.displays", testing::_))));

    // API <= 33 also omits external foldable displays even when a region is present.
    hw.hw_displayRegion_0_1_width = 1080;
    hw.hw_displayRegion_0_1_height = 2092;
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(testing::Return(33));
    const auto api33_props = getBootProperties(emu.config(), "lavapipe");
    EXPECT_THAT(api33_props, testing::Not(testing::Contains(testing::Pair(
                                     "androidboot.qemu.external.displays", testing::_))));
}

TEST(BootProperties, ExistingAdbKeyInjectsPubkeyWithoutKeygenOrServerKill) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path user_dir = home_dir / ".android";
    ASSERT_OK(android::base::file::mkdir_recursive(user_dir, 0750));
    sys.SetHomeDirectory(home_dir);

    const fs::path privkey_path = user_dir / "adbkey";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(privkey_path));
    std::string expected_pubkey;
    ASSERT_TRUE(::goldfish::adb::PubkeyFromPrivkey(privkey_path, &expected_pubkey));

    MockAdbKillServer adb_server;
    ASSERT_TRUE(adb_server.Valid());
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", absl::StrCat(adb_server.Port()));

    ScopedFakeAdbKeygen fake_keygen;

    FakeEmulator emu;
    emu.user_paths().user_directory = user_dir;
    emu.user_paths().is_shared_user_directory = true;
    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");

    EXPECT_THAT(props, testing::Contains(std::pair{std::string("androidboot.qemu.adb.pubkey"),
                                                   expected_pubkey}));
    EXPECT_EQ(fake_keygen.invocations(), 0);
    EXPECT_EQ(adb_server.ConnectionCount(), 0);
}

TEST(Initrd, GeneratesMissingDefaultKeyKillsStaleAdbServerAndInjectsPubkey) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path user_dir = home_dir / ".android";
    const fs::path sdk_dir = temp_path / "sdk";
    const fs::path content_path = temp_path / "content";
    const fs::path system_initrd = temp_path / "system" / "ramdisk.img";

    sys.SetHomeDirectory(home_dir);
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    ASSERT_OK(android::base::file::mkdir_recursive(content_path, 0755));
    ASSERT_OK(android::base::file::mkdir_recursive(system_initrd.parent_path(), 0755));
    std::ofstream{system_initrd} << "ramdisk-bytes";

    MockAdbKillServer adb_server;
    ASSERT_TRUE(adb_server.Valid());
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", absl::StrCat(adb_server.Port()));

    ScopedFakeAdbKeygen fake_keygen;

    FakeEmulator emu(AndroidOptions{.no_snapshot_load = true});
    emu.user_paths().user_directory = user_dir;
    emu.user_paths().sdk_directory = sdk_dir;
    emu.user_paths().is_shared_user_directory = true;

    auto hw = HardwareConfig();
    SystemImagePaths sys_paths;
    sys_paths.ramdisk_image = system_initrd;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths())
            .WillRepeatedly(testing::ReturnRef(sys_paths));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(testing::Return(content_path));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    GpuDevice gpu_dev("gpu0", snapshot_dev);
    ASSERT_OK(gpu_dev.initialize(emu.config()));
    InitrdDevice dev(gpu_dev, snapshot_dev);
    ASSERT_OK(dev.initialize(emu.config()));

    EXPECT_EQ(fake_keygen.invocations(), 1);
    EXPECT_EQ(adb_server.ReceivedRequest(), "0009host:kill");
    EXPECT_EQ(adb_server.ConnectionCount(), 1);
    EXPECT_TRUE(android::base::file::is_file(user_dir / "adbkey"));
    EXPECT_TRUE(android::base::file::is_file(user_dir / "adbkey.pub"));

    std::string expected_pubkey;
    ASSERT_TRUE(::goldfish::adb::PubkeyFromPrivkey(user_dir / "adbkey", &expected_pubkey));

    ASSERT_OK_AND_ASSIGN(
            const std::string initrd_bytes,
            android::base::file::read_whole_file(content_path / "initrd", /*binary=*/true));
    EXPECT_THAT(initrd_bytes, testing::HasSubstr(absl::StrCat("androidboot.qemu.adb.pubkey=\"",
                                                              expected_pubkey, "\"")));
}

TEST(BootProperties, GeneratesMissingDefaultKeyWhenNoAdbServerRunning) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path user_dir = home_dir / ".android";
    const fs::path sdk_dir = temp_path / "sdk";
    sys.SetHomeDirectory(home_dir);
    CreateStubAdbExecutable(sdk_dir / "platform-tools");

    int closed_port = 0;
    {
        android::base::ScopedSocket sock(android::base::socketTcp4LoopbackServer(0));
        ASSERT_TRUE(sock.valid());
        closed_port = android::base::socketGetPort(sock.get());
    }
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", absl::StrCat(closed_port));

    ScopedFakeAdbKeygen fake_keygen;

    FakeEmulator emu;
    emu.user_paths().user_directory = user_dir;
    emu.user_paths().sdk_directory = sdk_dir;
    emu.user_paths().is_shared_user_directory = true;
    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    ScopedWarningLogSink log_sink;
    const auto props = getBootProperties(emu.config(), "lavapipe");

    std::string expected_pubkey;
    ASSERT_TRUE(::goldfish::adb::PubkeyFromPrivkey(user_dir / "adbkey", &expected_pubkey));
    EXPECT_THAT(props, testing::Contains(std::pair{std::string("androidboot.qemu.adb.pubkey"),
                                                   expected_pubkey}));
    EXPECT_EQ(fake_keygen.invocations(), 1);
    EXPECT_THAT(log_sink.warnings(),
                testing::Not(testing::Contains(testing::HasSubstr("No ADB public key available"))));
    EXPECT_THAT(log_sink.warnings(), testing::Not(testing::Contains(testing::HasSubstr(
                                             "failed to stop running host ADB server"))));
}

TEST(BootProperties, LogsActionableWarningWhenNoAdbPublicKeyAvailable) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path user_dir = home_dir / ".android";
    sys.SetHomeDirectory(home_dir);
    sys.EnvSet("PATH", "");

    FakeEmulator emu;
    emu.user_paths().user_directory = user_dir;
    emu.user_paths().is_shared_user_directory = true;
    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    ScopedWarningLogSink log_sink;
    const auto props = getBootProperties(emu.config(), "lavapipe");

    EXPECT_THAT(props,
                testing::Not(testing::Contains(testing::Key("androidboot.qemu.adb.pubkey"))));
    EXPECT_THAT(log_sink.warnings(),
                testing::Contains(
                        testing::AllOf(testing::HasSubstr("No ADB public key available"),
                                       testing::HasSubstr("install Android SDK Platform-Tools"),
                                       testing::HasSubstr("restart the emulator"))));
}

TEST(BootProperties, GeneratesMissingDefaultKeyWarnsWhenAdbServerKillFails) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path user_dir = home_dir / ".android";
    const fs::path sdk_dir = temp_path / "sdk";
    sys.SetHomeDirectory(home_dir);
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", "invalid");

    ScopedFakeAdbKeygen fake_keygen;

    FakeEmulator emu;
    emu.user_paths().user_directory = user_dir;
    emu.user_paths().sdk_directory = sdk_dir;
    emu.user_paths().is_shared_user_directory = true;
    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    ScopedWarningLogSink log_sink;
    const auto props = getBootProperties(emu.config(), "lavapipe");

    std::string expected_pubkey;
    ASSERT_TRUE(::goldfish::adb::PubkeyFromPrivkey(user_dir / "adbkey", &expected_pubkey));
    EXPECT_THAT(props, testing::Contains(std::pair{std::string("androidboot.qemu.adb.pubkey"),
                                                   expected_pubkey}));
    EXPECT_EQ(fake_keygen.invocations(), 1);
    EXPECT_THAT(log_sink.warnings(),
                testing::Contains(testing::AllOf(
                        testing::HasSubstr("failed to stop running host ADB server"),
                        testing::HasSubstr("run 'adb kill-server' so it reloads the new key"))));
}

TEST(BootProperties, CustomUserDirGeneratesKeyWithoutKillingHostServer) {
    base::TestSystem sys("");
    const fs::path temp_path = sys.GetTempRoot()->Path();
    const fs::path home_dir = temp_path / "home";
    const fs::path custom_user_dir = temp_path / "custom_emulator_home";
    const fs::path sdk_dir = temp_path / "sdk";
    sys.SetHomeDirectory(home_dir);
    CreateStubAdbExecutable(sdk_dir / "platform-tools");

    MockAdbKillServer adb_server;
    ASSERT_TRUE(adb_server.Valid());
    sys.EnvSet("ANDROID_ADB_SERVER_PORT", absl::StrCat(adb_server.Port()));

    ScopedFakeAdbKeygen fake_keygen;

    FakeEmulator emu;
    emu.user_paths().user_directory = custom_user_dir;
    emu.user_paths().sdk_directory = sdk_dir;
    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), Name()).WillRepeatedly(testing::Return("mock_avd"));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    const auto props = getBootProperties(emu.config(), "lavapipe");

    std::string expected_pubkey;
    ASSERT_TRUE(::goldfish::adb::PubkeyFromPrivkey(custom_user_dir / "adbkey", &expected_pubkey));
    EXPECT_THAT(props, testing::Contains(std::pair{std::string("androidboot.qemu.adb.pubkey"),
                                                   expected_pubkey}));
    EXPECT_EQ(fake_keygen.invocations(), 1);
    EXPECT_EQ(adb_server.ConnectionCount(), 0);
}

}  // namespace android::goldfish::test
