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
#include <memory>

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

}  // namespace android::emulation::control
