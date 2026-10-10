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
#include <memory>
#include <string_view>
#include <vector>

#include "android/goldfish/qemu_vm_operations.h"
#include "android/goldfish/vm_interface.h"
#include "emulator/plugin/vminterface/test/test_vm_lock.h"
#include "goldfish/async/testing/test_event_loop.h"

extern "C" {
#include "emulator/plugin/vminterface/test/vm_mock.h"
}
#undef shutdown

namespace android {
namespace goldfish {

using ::goldfish::async::testing::TestEventLoop;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class QemuVmOperationsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Ensure we have a clean state before each test.
        mock_runstate_set(RUN_STATE_DEBUG);
        mock_shutdown_cause_set(SHUTDOWN_CAUSE_NONE);
        mock_iothread_locked_set(false);
        test_vm_lock_ = TestVmLock::GetInstance();
        test_vm_lock_->lock_count_ = 0;
        test_vm_lock_->unlock_count_ = 0;
        old_lock_ = VmLock::Set(test_vm_lock_);

        ASSERT_TRUE(VmLock::HasInstance());

        vm_ops_ = CreateQemuVmOperations(loop_.get());
        // Construction registers QEMU handlers under the VM lock; tests only
        // count the locking done by the operation under test.
        test_vm_lock_->lock_count_ = 0;
        test_vm_lock_->unlock_count_ = 0;
    }

    void TearDown() override {
        vm_ops_.reset();
        VmLock::Set(old_lock_);
        test_vm_lock_->Release();
        mock_runstate_set(RUN_STATE_DEBUG);
        mock_shutdown_cause_set(SHUTDOWN_CAUSE_NONE);
        mock_iothread_locked_set(false);
    }

    TestVmLock* test_vm_lock_ = nullptr;
    VmLock* old_lock_ = nullptr;
    // Declared before vm_ops_, so it outlives the VmOperations that posts to it.
    const std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    std::unique_ptr<VmOperations> vm_ops_;
};

TEST_F(QemuVmOperationsTest, Stop) {
    // Tests that the stop operation correctly pauses the VM and uses the VmLock.
    ASSERT_TRUE(vm_ops_->stop());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Start) {
    // Tests that the start operation correctly starts the VM and uses the VmLock.
    ASSERT_TRUE(vm_ops_->start());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, Reset) {
    // Tests that the reset operation correctly resets the VM and uses the VmLock.
    vm_ops_->reset();
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
}

TEST_F(QemuVmOperationsTest, Shutdown) {
    // Tests that the Shutdown operation correctly shuts down the VM and uses the VmLock.
    vm_ops_->Shutdown();
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);

    // Note: qemu_system_shutdown_request is asynchronous in QEMU.
    // The mock implementation only records the shutdown cause and does not
    // simulate an immediate transition to RUN_STATE_SHUTDOWN.
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_HOST_UI);
}

TEST_F(QemuVmOperationsTest, Pause) {
    // Tests that the pause operation correctly pauses the VM and uses the VmLock.
    ASSERT_TRUE(vm_ops_->pause());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Resume) {
    // Tests that the resume operation correctly resumes the VM and uses the VmLock.
    ASSERT_TRUE(vm_ops_->resume());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, IsRunning) {
    // Tests that isRunning correctly reports the VM's running state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_TRUE(vm_ops_->isRunning());
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_FALSE(vm_ops_->isRunning());
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_FALSE(vm_ops_->isRunning());
}

TEST_F(QemuVmOperationsTest, GetRunState) {
    // Tests that getRunState correctly returns the VM's run state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_EQ(vm_ops_->getRunState(), EmuRunState::Running);
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_EQ(vm_ops_->getRunState(), EmuRunState::Paused);
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_EQ(vm_ops_->getRunState(), EmuRunState::Debug);
}

TEST_F(QemuVmOperationsTest, SystemShutdownRequest) {
    // Tests that systemShutdownRequest correctly sets the shutdown cause.
    vm_ops_->systemShutdownRequest(QemuShutdownCause::GuestShutdown);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_GUEST_SHUTDOWN);

    vm_ops_->systemShutdownRequest(QemuShutdownCause::HostError);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_HOST_ERROR);
}

TEST_F(QemuVmOperationsTest, RunStateChangeCallback) {
    std::vector<EmuRunState> received_states;
    auto& events = vm_ops_->RunStateEvents();
    auto id = events.AddCallback([&](EmuRunState state) { received_states.push_back(state); });

    mock_fire_vm_change_state(true, RUN_STATE_RUNNING);
    mock_fire_vm_change_state(false, RUN_STATE_PAUSED);
    loop_->RunAll();

    ASSERT_EQ(received_states.size(), 2);
    EXPECT_EQ(received_states[0], EmuRunState::Running);
    EXPECT_EQ(received_states[1], EmuRunState::Paused);

    events.RemoveCallback(id);
    mock_fire_vm_change_state(true, RUN_STATE_RUNNING);
    loop_->RunAll();
    EXPECT_EQ(received_states.size(), 2);
}

TEST_F(QemuVmOperationsTest, RunStateChangeScopedCallback) {
    std::vector<EmuRunState> received_states;
    {
        auto handle = android::base::eventing::MakeScopedCallback(
                vm_ops_->RunStateEvents(),
                [&](EmuRunState state) { received_states.push_back(state); });

        mock_fire_vm_change_state(true, RUN_STATE_RUNNING);
        loop_->RunAll();
        EXPECT_EQ(received_states.size(), 1);
        EXPECT_EQ(received_states[0], EmuRunState::Running);
    }
    // Handle went out of scope, callback should be automatically unregistered.
    mock_fire_vm_change_state(false, RUN_STATE_PAUSED);
    loop_->RunAll();
    EXPECT_EQ(received_states.size(), 1);
}

TEST_F(QemuVmOperationsTest, RunStateEventsAreDeliveredOnTheLoop) {
    std::vector<EmuRunState> received_states;
    bool delivered_on_loop = false;
    auto handle = android::base::eventing::MakeScopedCallback(
            vm_ops_->RunStateEvents(), [&](EmuRunState state) {
                delivered_on_loop = loop_->IsOnLoopThread();
                received_states.push_back(state);
            });

    mock_fire_vm_change_state(true, RUN_STATE_RUNNING);
    // The thread that changed the run state holds the BQL, so subscribers must not run on it.
    EXPECT_THAT(received_states, IsEmpty());

    loop_->RunAll();
    EXPECT_THAT(received_states, ElementsAre(EmuRunState::Running));
    EXPECT_TRUE(delivered_on_loop);
}

TEST_F(QemuVmOperationsTest, CallbackRemovedWhileItsEventIsQueuedIsNotCalled) {
    std::vector<EmuRunState> received_states;
    {
        auto handle = android::base::eventing::MakeScopedCallback(
                vm_ops_->RunStateEvents(),
                [&](EmuRunState state) { received_states.push_back(state); });
        mock_fire_vm_change_state(true, RUN_STATE_RUNNING);
        // The handle goes away while its event is still queued on the loop.
    }
    loop_->RunAll();

    EXPECT_THAT(received_states, IsEmpty());
}

TEST_F(QemuVmOperationsTest, EventsQueuedWhenVmOperationsIsDestroyedAreDropped) {
    std::vector<EmuRunState> received_states;
    vm_ops_->RunStateEvents().AddCallback(
            [&](EmuRunState state) { received_states.push_back(state); });
    mock_fire_vm_change_state(true, RUN_STATE_RUNNING);

    vm_ops_.reset();
    loop_->RunAll();

    EXPECT_THAT(received_states, IsEmpty());
}

using QemuVmOperationsDeathTest = QemuVmOperationsTest;

TEST_F(QemuVmOperationsDeathTest, RunStateChangeOnTheLoopThreadDies) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    auto change_run_state = [] { mock_fire_vm_change_state(true, RUN_STATE_RUNNING); };
    // On the loop thread the subscribers would run inline, under the BQL.
    EXPECT_DEBUG_DEATH(
            {
                loop_->Post(change_run_state).IgnoreError();
                loop_->RunAll();
            },
            "event loop thread");
}

TEST_F(QemuVmOperationsTest, DestructorUnregistersRunStateHandler) {
    ASSERT_TRUE(mock_has_vm_change_state_handler());

    vm_ops_.reset();

    EXPECT_FALSE(mock_has_vm_change_state_handler());
}

}  // namespace goldfish
}  // namespace android
