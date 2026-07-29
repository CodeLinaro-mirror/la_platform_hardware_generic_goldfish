// Copyright 2025 The Android Open Source Project
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

#include <array>
#include <string_view>

#include "android/goldfish/vm_interface.h"
#include "emulator/plugin/vminterface/test/test_vm_lock.h"

extern "C" {
#include "emulator/plugin/vminterface/test/vm_mock.h"
}
#undef shutdown

namespace android {
namespace goldfish {

class QemuVmOperationsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Ensure we have a clean state before each test.
        mock_runstate_set(RUN_STATE_DEBUG);
        mock_shutdown_cause_set(SHUTDOWN_CAUSE_NONE);
        mock_iothread_locked_set(false);
        mTestVmLock = TestVmLock::getInstance();
        mTestVmLock->mLockCount = 0;
        mTestVmLock->mUnlockCount = 0;
        VmLock::set(mTestVmLock);
        ASSERT_TRUE(VmLock::hasInstance());
    }

    void TearDown() override {
        VmLock::set(mOldLock);
        mTestVmLock->release();
        mock_runstate_set(RUN_STATE_DEBUG);
        mock_shutdown_cause_set(SHUTDOWN_CAUSE_NONE);
        mock_iothread_locked_set(false);
    }

    TestVmLock* mTestVmLock;
    VmLock* mOldLock;
    VmOperations* vmOps = VmOperations::qemuVmOperations();
};

TEST_F(QemuVmOperationsTest, Stop) {
    // Tests that the stop operation correctly pauses the VM and uses the VmLock.
    ASSERT_TRUE(vmOps->stop());
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Start) {
    // Tests that the start operation correctly starts the VM and uses the VmLock.
    ASSERT_TRUE(vmOps->start());
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, Reset) {
    // Tests that the reset operation correctly resets the VM and uses the VmLock.
    vmOps->reset();
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
}

TEST_F(QemuVmOperationsTest, Shutdown) {
    // Tests that the Shutdown operation correctly shuts down the VM and uses the VmLock.
    vmOps->Shutdown();
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);

    // Note: qemu_system_shutdown_request is asynchronous in QEMU.
    // The mock implementation only records the shutdown cause and does not
    // simulate an immediate transition to RUN_STATE_SHUTDOWN.
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_HOST_UI);
}

TEST_F(QemuVmOperationsTest, Pause) {
    // Tests that the pause operation correctly pauses the VM and uses the VmLock.
    ASSERT_TRUE(vmOps->pause());
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Resume) {
    // Tests that the resume operation correctly resumes the VM and uses the VmLock.
    ASSERT_TRUE(vmOps->resume());
    ASSERT_EQ(mTestVmLock->mLockCount, 1);
    ASSERT_EQ(mTestVmLock->mUnlockCount, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, IsRunning) {
    // Tests that isRunning correctly reports the VM's running state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_TRUE(vmOps->isRunning());
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_FALSE(vmOps->isRunning());
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_FALSE(vmOps->isRunning());
}

TEST_F(QemuVmOperationsTest, GetRunState) {
    // Tests that getRunState correctly returns the VM's run state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_EQ(vmOps->getRunState(), EmuRunState::Running);
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_EQ(vmOps->getRunState(), EmuRunState::Paused);
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_EQ(vmOps->getRunState(), EmuRunState::Debug);
}

TEST_F(QemuVmOperationsTest, SystemShutdownRequest) {
    // Tests that systemShutdownRequest correctly sets the shutdown cause.
    vmOps->systemShutdownRequest(QemuShutdownCause::GuestShutdown);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_GUEST_SHUTDOWN);

    vmOps->systemShutdownRequest(QemuShutdownCause::HostError);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_HOST_ERROR);
}

}  // namespace goldfish
}  // namespace android
