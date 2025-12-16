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

#include "emulator/grpc/services/emulator_controller/server/vm_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "gtest/gtest.h"

#include "android/goldfish/vm_interface.h"
#include "emulator/grpc/services/emulator_controller/server/test/GrpcServiceTest.h"
#include "emulator_controller.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {

using ::android::goldfish::EmuRunState;
using ::android::goldfish::QemuShutdownCause;
using GoldfishVmConfiguration = ::android::goldfish::VmConfiguration;
using ::android::goldfish::VmHypervisorType;
using ::android::goldfish::VmOperations;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

// Mock VmOperations for testing
class MockVmOperations : public VmOperations {
  public:
    MOCK_METHOD(bool, stop, (), (override));
    MOCK_METHOD(bool, start, (), (override));
    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(void, Shutdown, (), (override));
    MOCK_METHOD(bool, pause, (), (override));
    MOCK_METHOD(bool, resume, (), (override));
    MOCK_METHOD(bool, isRunning, (), (override));
    MOCK_METHOD(GoldfishVmConfiguration, getConfiguration, (), (override));
    MOCK_METHOD(EmuRunState, getRunState, (), (override));
    MOCK_METHOD(void, systemShutdownRequest, (QemuShutdownCause reason), (override));
};

struct VmServiceTest : public ::testing::Test {
    void SetUp() override { vmService = std::make_unique<VmServiceImpl>(&vmOperations); }

    void TearDown() override { vmService.reset(); }

    MockVmOperations vmOperations;
    std::unique_ptr<VmServiceImpl> vmService;
};

TEST_F(VmServiceTest, SetVmStateReset) {
    EXPECT_CALL(vmOperations, reset()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RESET);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateShutdown) {
    EXPECT_CALL(vmOperations, Shutdown()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::SHUTDOWN);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateTerminate) {
    // This will kill the process!
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    VmRunState request;
    request.set_state(VmRunState::TERMINATE);

    EXPECT_DEATH_IF_SUPPORTED(vmService->setVmState(request), ".*");
}

TEST_F(VmServiceTest, SetVmStatePaused) {
    EXPECT_CALL(vmOperations, pause()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::PAUSED);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateRunning) {
    EXPECT_CALL(vmOperations, resume()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RUNNING);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateRestart) {
    EXPECT_CALL(vmOperations, reset()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RESTART);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateStart) {
    EXPECT_CALL(vmOperations, start()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::START);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateStop) {
    EXPECT_CALL(vmOperations, stop()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::STOP);

    ASSERT_GRPC_STATUS(vmService->setVmState(request));
}

TEST_F(VmServiceTest, GetVmStatePaused) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Paused));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateSuspended) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Suspended));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateRestoreVm) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::RestoreVm));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateRunning) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Running));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RUNNING);
}

TEST_F(VmServiceTest, GetVmStateSaveVm) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::SaveVm));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::SAVE_VM);
}

TEST_F(VmServiceTest, GetVmStateShutdown) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Shutdown));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::SHUTDOWN);
}

TEST_F(VmServiceTest, GetVmStateInternalError) {
    VmRunState reply;

    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::GuestPanicked));
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::InternalError));
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::IoError));
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);
}

TEST_F(VmServiceTest, GetVmStateDebug) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Debug));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateInMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::InMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePostMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::PostMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePreLaunch) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::PreLaunch));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateFinishMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::FinishMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateWatchdog) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Watchdog));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateColo) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Colo));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateDefault) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Max));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vmService->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
