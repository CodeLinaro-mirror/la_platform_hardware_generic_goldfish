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

#include "vm_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "gtest/gtest.h"

#include "android/goldfish/vm_interface.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "test/grpc_service_test.h"

namespace android {
namespace emulation {
namespace control {

using ::android::goldfish::EmuRunState;
using ::android::goldfish::QemuShutdownCause;
using GoldfishVmConfiguration = ::android::goldfish::VmConfiguration;
using ::android::goldfish::VmHypervisorType;
using ::android::goldfish::VmOperations;
using ::goldfish::async::testing::TestEventLoop;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

// Mock VmOperations for testing
class MockVmOperations : public VmOperations {
  public:
    explicit MockVmOperations(std::unique_ptr<TestEventLoop> loop = TestEventLoop::Create())
            : VmOperations(loop.get()), loop_(std::move(loop)) {}

    MOCK_METHOD(bool, Stop, (), (override));
    MOCK_METHOD(bool, Start, (), (override));
    MOCK_METHOD(void, Reset, (), (override));
    MOCK_METHOD(void, Shutdown, (), (override));
    MOCK_METHOD(bool, Pause, (), (override));
    MOCK_METHOD(bool, Resume, (), (override));
    MOCK_METHOD(bool, IsRunning, (), (override));
    MOCK_METHOD(GoldfishVmConfiguration, GetConfiguration, (), (override));
    MOCK_METHOD(EmuRunState, GetRunState, (), (override));
    MOCK_METHOD(void, SystemShutdownRequest, (QemuShutdownCause reason), (override));
    MOCK_METHOD(absl::Status, SaveSnapshot, (std::string name, bool overwrite), (override));
    MOCK_METHOD(absl::Status, LoadSnapshot, (std::string id_or_name, bool and_resume), (override));
    MOCK_METHOD(void, LoadSnapshotResume, (EmuRunState state), (override));
    MOCK_METHOD(absl::Status, DeleteSnapshot, (std::string id_or_name), (override));
    MOCK_METHOD(absl::Status, ListSnapshots, (SnapshotEntrySink sink), (override));

  private:
    const std::unique_ptr<TestEventLoop> loop_;
};

struct VmServiceTest : public ::testing::Test {
    void SetUp() override { vm_service = std::make_unique<VmServiceImpl>(&vm_operations); }

    void TearDown() override { vm_service.reset(); }

    MockVmOperations vm_operations;
    std::unique_ptr<VmServiceImpl> vm_service;
};

TEST_F(VmServiceTest, SetVmStateReset) {
    EXPECT_CALL(vm_operations, Reset()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RESET);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateShutdown) {
    EXPECT_CALL(vm_operations, Shutdown()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::SHUTDOWN);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateTerminate) {
    // This will kill the process!
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    VmRunState request;
    request.set_state(VmRunState::TERMINATE);

    EXPECT_DEATH_IF_SUPPORTED(vm_service->setVmState(request), ".*");
}

TEST_F(VmServiceTest, SetVmStatePaused) {
    EXPECT_CALL(vm_operations, Pause()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::PAUSED);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateRunning) {
    EXPECT_CALL(vm_operations, Resume()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RUNNING);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateRestart) {
    EXPECT_CALL(vm_operations, Reset()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::RESTART);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateStart) {
    EXPECT_CALL(vm_operations, Start()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::START);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, SetVmStateStop) {
    EXPECT_CALL(vm_operations, Stop()).Times(1);

    VmRunState request;
    request.set_state(VmRunState::STOP);

    ASSERT_GRPC_STATUS(vm_service->setVmState(request));
}

TEST_F(VmServiceTest, GetVmStatePaused) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Paused));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateSuspended) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Suspended));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::PAUSED);
}

TEST_F(VmServiceTest, GetVmStateRestoreVm) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::RestoreVm));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateRunning) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Running));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RUNNING);
}

TEST_F(VmServiceTest, GetVmStateSaveVm) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::SaveVm));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::SAVE_VM);
}

TEST_F(VmServiceTest, GetVmStateShutdown) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Shutdown));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::SHUTDOWN);
}

TEST_F(VmServiceTest, GetVmStateInternalError) {
    VmRunState reply;

    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::GuestPanicked));
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::InternalError));
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);

    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::IoError));
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::INTERNAL_ERROR);
}

TEST_F(VmServiceTest, GetVmStateDebug) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Debug));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateInMigrate) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::InMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePostMigrate) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::PostMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStatePreLaunch) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::PreLaunch));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateFinishMigrate) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::FinishMigrate));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::RESTORE_VM);
}

TEST_F(VmServiceTest, GetVmStateWatchdog) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Watchdog));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateColo) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Colo));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

TEST_F(VmServiceTest, GetVmStateDefault) {
    EXPECT_CALL(vm_operations, GetRunState()).WillOnce(Return(EmuRunState::Max));

    VmRunState reply;
    ASSERT_GRPC_STATUS(vm_service->getVmState(&reply));
    EXPECT_EQ(reply.state(), VmRunState::UNKNOWN);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
