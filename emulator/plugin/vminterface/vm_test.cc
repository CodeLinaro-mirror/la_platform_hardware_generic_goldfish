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
#include <cerrno>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/status/status_matchers.h"

#include "android/goldfish/qemu_vm_operations.h"
#include "android/goldfish/vm_interface.h"
#include "android/status/status_matcher_macros.h"
#include "emulator/plugin/vminterface/test/test_vm_lock.h"
#include "goldfish/async/testing/test_event_loop.h"

extern "C" {
#include "emulator/plugin/vminterface/test/vm_mock.h"
}
#undef shutdown

namespace android {
namespace goldfish {

using ::absl_testing::StatusIs;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

class QemuVmOperationsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Ensure we have a clean state before each test.
        mock_runstate_set(RUN_STATE_DEBUG);
        mock_shutdown_cause_set(SHUTDOWN_CAUSE_NONE);
        mock_iothread_locked_set(false);
        mock_vm_stop_return_set(0);
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
        mock_vm_stop_return_set(0);
    }

    TestVmLock* test_vm_lock_ = nullptr;
    VmLock* old_lock_ = nullptr;
    // Declared before vm_ops_, so it outlives the VmOperations that posts to it.
    const std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    std::unique_ptr<VmOperations> vm_ops_;
};

TEST_F(QemuVmOperationsTest, Stop) {
    // Tests that the Stop operation correctly pauses a running VM and uses the VmLock.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_OK(vm_ops_->Stop());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Start) {
    // Tests that the Start operation correctly starts the VM and uses the VmLock.
    ASSERT_OK(vm_ops_->Start());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, Reset) {
    // Tests that the Reset operation correctly resets the VM and uses the VmLock.
    ASSERT_OK(vm_ops_->Reset());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
}

TEST_F(QemuVmOperationsTest, Shutdown) {
    // Tests that the Shutdown operation correctly shuts down the VM and uses the VmLock.
    ASSERT_OK(vm_ops_->Shutdown());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);

    // Note: qemu_system_shutdown_request is asynchronous in QEMU.
    // The mock implementation only records the shutdown cause and does not
    // simulate an immediate transition to RUN_STATE_SHUTDOWN.
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_HOST_UI);
}

TEST_F(QemuVmOperationsTest, Pause) {
    // Tests that the Pause operation correctly pauses a running VM and uses the VmLock.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_OK(vm_ops_->Pause());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_PAUSED);
}

TEST_F(QemuVmOperationsTest, Resume) {
    // Tests that the Resume operation correctly resumes the VM and uses the VmLock.
    ASSERT_OK(vm_ops_->Resume());
    ASSERT_EQ(test_vm_lock_->lock_count_, 1);
    ASSERT_EQ(test_vm_lock_->unlock_count_, 1);
    ASSERT_EQ(runstate_get(), RUN_STATE_RUNNING);
}

TEST_F(QemuVmOperationsTest, StartResumePauseStopAndShutdownAreIdempotent) {
    mock_runstate_set(RUN_STATE_RUNNING);
    EXPECT_OK(vm_ops_->Start());
    EXPECT_OK(vm_ops_->Resume());

    mock_runstate_set(RUN_STATE_PAUSED);
    EXPECT_OK(vm_ops_->Pause());
    EXPECT_OK(vm_ops_->Stop());

    mock_runstate_set(RUN_STATE_SHUTDOWN);
    EXPECT_OK(vm_ops_->Shutdown());
}

TEST_F(QemuVmOperationsTest, StartAndResumeFailInConflictingStates) {
    for (RunState state : {RUN_STATE_SHUTDOWN, RUN_STATE_SAVE_VM, RUN_STATE_RESTORE_VM,
                           RUN_STATE_INTERNAL_ERROR, RUN_STATE_GUEST_PANICKED}) {
        mock_runstate_set(state);
        EXPECT_THAT(vm_ops_->Start(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                               HasSubstr("Running, Paused, Suspended")));
        EXPECT_THAT(vm_ops_->Resume(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                                HasSubstr("Running, Paused, Suspended")));
        EXPECT_EQ(runstate_get(), state);
    }

    mock_runstate_set(RUN_STATE_SHUTDOWN);
    EXPECT_THAT(vm_ops_->Resume(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                            AllOf(HasSubstr("Shutdown"),
                                                  HasSubstr("Launch a new emulator instance"))));

    mock_runstate_set(RUN_STATE_SAVE_VM);
    EXPECT_THAT(
            vm_ops_->Resume(),
            StatusIs(absl::StatusCode::kFailedPrecondition,
                     AllOf(HasSubstr("SaveVm"), HasSubstr("Wait for the operation to complete"))));

    mock_runstate_set(RUN_STATE_GUEST_PANICKED);
    EXPECT_THAT(vm_ops_->Resume(),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         AllOf(HasSubstr("GuestPanicked"), HasSubstr("Reset the VM"))));
}

TEST_F(QemuVmOperationsTest, PauseAndStopFailInConflictingStates) {
    for (RunState state : {RUN_STATE_DEBUG, RUN_STATE_PRELAUNCH, RUN_STATE_POSTMIGRATE,
                           RUN_STATE_SHUTDOWN, RUN_STATE_SAVE_VM, RUN_STATE_RESTORE_VM,
                           RUN_STATE_INTERNAL_ERROR, RUN_STATE_GUEST_PANICKED}) {
        mock_runstate_set(state);
        EXPECT_THAT(vm_ops_->Pause(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                               HasSubstr("Running, Paused, or Suspended")));
        EXPECT_THAT(vm_ops_->Stop(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                              HasSubstr("Running, Paused, or Suspended")));
        EXPECT_EQ(runstate_get(), state);
    }

    mock_runstate_set(RUN_STATE_SHUTDOWN);
    EXPECT_THAT(vm_ops_->Pause(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                           AllOf(HasSubstr("Shutdown"),
                                                 HasSubstr("Launch a new emulator instance"))));

    mock_runstate_set(RUN_STATE_SAVE_VM);
    EXPECT_THAT(vm_ops_->Pause(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                           AllOf(HasSubstr("SaveVm"),
                                                 HasSubstr("Wait for the operation to complete"))));

    mock_runstate_set(RUN_STATE_DEBUG);
    EXPECT_THAT(vm_ops_->Pause(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                           AllOf(HasSubstr("Debug"), HasSubstr("Reset the VM"))));
}

TEST_F(QemuVmOperationsTest, PauseAndStopPropagateVmStopErrors) {
    mock_runstate_set(RUN_STATE_RUNNING);
    mock_vm_stop_return_set(-ENOSPC);

    EXPECT_THAT(vm_ops_->Pause(),
                StatusIs(absl::StatusCode::kResourceExhausted,
                         AllOf(HasSubstr("Cannot pause VM"), HasSubstr("check host storage"))));
    EXPECT_THAT(vm_ops_->Stop(),
                StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("Cannot pause VM")));

    mock_vm_stop_return_set(1);
    EXPECT_THAT(vm_ops_->Pause(), StatusIs(absl::StatusCode::kInternal,
                                           AllOf(HasSubstr("device stop callback failed"),
                                                 HasSubstr("Reset the VM"))));
    EXPECT_THAT(vm_ops_->Stop(),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("device stop callback failed")));
}

TEST_F(QemuVmOperationsTest, ResetAndShutdownFailInConflictingStates) {
    mock_runstate_set(RUN_STATE_SHUTDOWN);
    EXPECT_THAT(vm_ops_->Reset(),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         AllOf(HasSubstr("Shutdown"),
                               HasSubstr("Running, Paused, Suspended, Debug, or fault state"),
                               HasSubstr("Launch a new emulator instance"))));

    for (RunState state : {RUN_STATE_SAVE_VM, RUN_STATE_RESTORE_VM}) {
        mock_runstate_set(state);
        EXPECT_THAT(vm_ops_->Reset(),
                    StatusIs(absl::StatusCode::kFailedPrecondition,
                             AllOf(HasSubstr("Running, Paused, Suspended, Debug, or fault state"),
                                   HasSubstr("Wait for the operation to complete"))));
        EXPECT_THAT(vm_ops_->Shutdown(),
                    StatusIs(absl::StatusCode::kFailedPrecondition,
                             AllOf(HasSubstr("Running, Paused, Suspended, Debug, or fault state"),
                                   HasSubstr("Wait for the operation to complete"))));
    }
}

TEST_F(QemuVmOperationsTest, IsRunning) {
    // Tests that IsRunning correctly reports the VM's running state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_TRUE(vm_ops_->IsRunning());
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_FALSE(vm_ops_->IsRunning());
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_FALSE(vm_ops_->IsRunning());
}

TEST_F(QemuVmOperationsTest, GetRunState) {
    // Tests that GetRunState correctly returns the VM's run state.
    mock_runstate_set(RUN_STATE_RUNNING);
    ASSERT_EQ(vm_ops_->GetRunState(), EmuRunState::Running);
    mock_runstate_set(RUN_STATE_PAUSED);
    ASSERT_EQ(vm_ops_->GetRunState(), EmuRunState::Paused);
    mock_runstate_set(RUN_STATE_DEBUG);
    ASSERT_EQ(vm_ops_->GetRunState(), EmuRunState::Debug);
}

TEST_F(QemuVmOperationsTest, SystemShutdownRequest) {
    // Tests that SystemShutdownRequest correctly sets the shutdown cause.
    vm_ops_->SystemShutdownRequest(QemuShutdownCause::GuestShutdown);
    ASSERT_EQ(mock_shutdown_cause_get(), SHUTDOWN_CAUSE_GUEST_SHUTDOWN);

    vm_ops_->SystemShutdownRequest(QemuShutdownCause::HostError);
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
