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

#include "status_service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "android/cpu/cpu_accelerator.h"
#include "android/goldfish/fake_hardware_config.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/avd_info/avd_info.h"
#include "test/GrpcServiceTest.h"

namespace android::emulation::control {

using ::goldfish::avd_universe::guest_status::GuestStatus;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;

class StatusServiceWrapper : public EmulatorController::Service {
  public:
    StatusServiceWrapper(StatusServiceImpl& statusService) : mStatusService(statusService) {}

    Status getStatus(ServerContext* /*context*/, const Empty* /*request*/,
                     EmulatorStatus* reply) override {
        return mStatusService.getStatus(reply);
    }

  private:
    StatusServiceImpl& mStatusService;
};

class StatusServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        // Mock a deterministic CPU accelerator to none by default
        android::SetCurrentCpuAcceleratorForTesting(
                android::CPU_ACCELERATOR_NONE, android::ANDROID_CPU_ACCELERATION_NO_CPU_SUPPORT,
                "No CPU acceleration");

        mGuestStatus.heartbeat.SetValue(0);
        mGuestStatus.bootcomplete.SetValue(absl::UnixEpoch());
        mAvdProperties.hw_config = android::goldfish::FakeHardwareConfig::GetHwConfig();
        mAvdProperties.avd_api = 35;
        mAvdProperties.avd_name = "fake-avd";
        mAvdProperties.avd_id = "fake-avd-id";
        mStatusService = std::make_unique<StatusServiceImpl>(mGuestStatus, mAvdProperties);
        mServiceWrapper = std::make_unique<StatusServiceWrapper>(*mStatusService);

        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return mServiceWrapper.get(); }

  protected:
    GuestStatus mGuestStatus;
    ::goldfish::avd_info::AvdProperties mAvdProperties;
    std::unique_ptr<StatusServiceImpl> mStatusService;
    std::unique_ptr<StatusServiceWrapper> mServiceWrapper;
};

TEST_F(StatusServiceTest, GetStatusInitialState) {
    Empty request;
    EmulatorStatus reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getStatus(context.get(), request, &reply));

    EXPECT_FALSE(reply.booted());
    EXPECT_EQ(reply.heartbeat(), 0);
    EXPECT_GT(reply.uptime(), 0);

    // Check guestconfig
    auto guestConfig = reply.guestconfig();
    EXPECT_EQ(guestConfig["multidisplay"], "unavailable");
    EXPECT_EQ(guestConfig["androidVersion"], "API 35");
    EXPECT_EQ(guestConfig["hypervisorVersion"], "None");

    std::string expectedAvdDetails =
            "Name: fake-avd\n"
            "CPU/ABI: \n"
            "Path: \n"
            "Target: API level 35\n"
            "Build SDK: \n"
            "Build ID: \n"
            "Build Flavour: \n";
    EXPECT_EQ(guestConfig["avdDetails"], expectedAvdDetails);

    // Check hardwareconfig
    EXPECT_GT(reply.hardwareconfig().entry_size(), 0);

    // Find specific entry, e.g. avd.api_level
    bool foundApiLevel = false;
    for (const auto& entry : reply.hardwareconfig().entry()) {
        if (entry.key() == "avd.api_level") {
            EXPECT_EQ(entry.value(), "35");
            foundApiLevel = true;
        }
    }
    EXPECT_TRUE(foundApiLevel);

    // Check platformconfig
    auto platformConfig = reply.platformconfig();
    EXPECT_EQ(platformConfig["avd.api_level"], "35");
    EXPECT_EQ(platformConfig["hw.cpu.arch"], "arm64");
}

TEST_F(StatusServiceTest, GetStatusBootedState) {
    mGuestStatus.bootcomplete.SetValue(absl::Now());
    mGuestStatus.heartbeat.SetValue(42);

    Empty request;
    EmulatorStatus reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getStatus(context.get(), request, &reply));

    EXPECT_TRUE(reply.booted());
    EXPECT_EQ(reply.heartbeat(), 42);
}

TEST_F(StatusServiceTest, GetStatusWithCpuAcceleration) {
    android::SetCurrentCpuAcceleratorForTesting(android::CPU_ACCELERATOR_KVM,
                                                android::ANDROID_CPU_ACCELERATION_READY,
                                                "KVM is installed", "12");

    Empty request;
    EmulatorStatus reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getStatus(context.get(), request, &reply));

    auto guestConfig = reply.guestconfig();
    EXPECT_EQ(guestConfig["hypervisorVersion"], "kvm 12.0.0");
}

TEST_F(StatusServiceTest, GetStatusWithAvdConfigIni) {
    // 1. Create a temporary config.ini file
    std::filesystem::path tempDir = std::filesystem::path(testing::TempDir()) / "fake_avd_content";
    std::filesystem::create_directories(tempDir);
    std::filesystem::path configIniPath = tempDir / "config.ini";

    std::ofstream out(configIniPath);
    ASSERT_TRUE(out.is_open());
    out << "hw.cpu.arch = x86_64\n";
    out << "disk.dataPartition.size = 2G\n";
    out << "AvdId = filter-me-out\n";
    out << "avd.id = filter-me-out-too\n";
    out << "avd.name = filter-me-out-three\n";
    out << "custom.key.example = dynamic_value\n";
    out.close();

    // 2. Assign temporary directory to avd content path
    mAvdProperties.avd_content_path = tempDir;

    Empty request;
    EmulatorStatus reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getStatus(context.get(), request, &reply));

    auto guestConfig = reply.guestconfig();
    std::string details = guestConfig["avdDetails"];

    // Static properties should still exist
    EXPECT_NE(details.find("Name: fake-avd\n"), std::string::npos);
    EXPECT_NE(details.find("Target: API level 35\n"), std::string::npos);

    // Dynamic config.ini properties should exist
    EXPECT_NE(details.find("hw.cpu.arch: x86_64\n"), std::string::npos);
    EXPECT_NE(details.find("disk.dataPartition.size: 2G\n"), std::string::npos);
    EXPECT_NE(details.find("custom.key.example: dynamic_value\n"), std::string::npos);

    // Filtered out properties should NOT exist
    EXPECT_EQ(details.find("AvdId"), std::string::npos);
    EXPECT_EQ(details.find("avd.id"), std::string::npos);
    EXPECT_EQ(details.find("avd.name"), std::string::npos);

    // Cleanup temp files
    std::filesystem::remove_all(tempDir);
}

TEST_F(StatusServiceTest, GetStatusWithCpuAccelerationUnknownVersion) {
    android::SetCurrentCpuAcceleratorForTesting(android::CPU_ACCELERATOR_KVM,
                                                android::ANDROID_CPU_ACCELERATION_READY,
                                                "KVM is installed");

    Empty request;
    EmulatorStatus reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getStatus(context.get(), request, &reply));

    auto guestConfig = reply.guestconfig();
    EXPECT_EQ(guestConfig["hypervisorVersion"], "None");
}

}  // namespace android::emulation::control
