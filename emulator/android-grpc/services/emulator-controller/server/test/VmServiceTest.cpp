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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "GrpcServiceTest.h"
#include "gtest/gtest.h"

#include "android/emulation/control/VmService.h"
#include "android/goldfish/vm/VmInterface.h"
#include "hardware/generic/goldfish/emulator/android-grpc/services/emulator-controller/proto/emulator_controller.grpc.pb.h"

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
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, pause, (), (override));
    MOCK_METHOD(bool, resume, (), (override));
    MOCK_METHOD(bool, isRunning, (), (override));
    MOCK_METHOD(GoldfishVmConfiguration, getConfiguration, (), (override));
    MOCK_METHOD(EmuRunState, getRunState, (), (override));
    MOCK_METHOD(void, systemShutdownRequest, (QemuShutdownCause reason), (override));
};

class VmServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        vmService = std::make_unique<VmServiceImpl>(&vmOperations);
        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return vmService.get(); }

    MockVmOperations vmOperations;
    std::unique_ptr<VmServiceImpl> vmService;
};

TEST_F(VmServiceTest, SetVmStateReset) {
    EXPECT_CALL(vmOperations, reset()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::RESET);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateShutdown) {
    EXPECT_CALL(vmOperations, shutdown()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::SHUTDOWN);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateTerminate) {
    // This will kill the process!
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::TERMINATE);

    auto context = getContextWithTimeout();
    EXPECT_DEATH_IF_SUPPORTED(mStub->setVmState(context.get(), request, &reply), ".*");
}

TEST_F(VmServiceTest, SetVmStatePaused) {
    EXPECT_CALL(vmOperations, pause()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::PAUSED);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateRunning) {
    EXPECT_CALL(vmOperations, resume()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::RUNNING);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateRestart) {
    EXPECT_CALL(vmOperations, reset()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::RESTART);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateStart) {
    EXPECT_CALL(vmOperations, start()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::START);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, SetVmStateStop) {
    EXPECT_CALL(vmOperations, stop()).Times(1);

    VmRunState request;
    ::google::protobuf::Empty reply;
    request.set_state(VmRunState::STOP);

    auto context = getContextWithTimeout();
    Status status = mStub->setVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
}

TEST_F(VmServiceTest, GetVmStatePaused) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Paused));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateSuspended) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Suspended));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateRestoreVm) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::RestoreVm));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateRunning) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Running));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::RUNNING);
}

TEST_F(VmServiceTest, GetVmStateSaveVm) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::SaveVm));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::SAVE_VM);
}

TEST_F(VmServiceTest, GetVmStateShutdown) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Shutdown));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::SHUTDOWN);
}

TEST_F(VmServiceTest, GetVmStateInternalError) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::GuestPanicked));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::InternalError));
    context = getContextWithTimeout();
    status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::IoError));
    context = getContextWithTimeout();
    status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);
}

TEST_F(VmServiceTest, GetVmStateDebug) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Debug));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateInMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::InMigrate));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePostMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::PostMigrate));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePreLaunch) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::PreLaunch));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateFinishMigrate) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::FinishMigrate));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateWatchdog) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Watchdog));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateColo) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Colo));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateDefault) {
    EXPECT_CALL(vmOperations, getRunState()).WillOnce(Return(EmuRunState::Max));

    ::google::protobuf::Empty request;
    VmRunState reply;

    auto context = getContextWithTimeout();
    Status status = mStub->getVmState(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_code() << ", msg: " << status.error_message();
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
