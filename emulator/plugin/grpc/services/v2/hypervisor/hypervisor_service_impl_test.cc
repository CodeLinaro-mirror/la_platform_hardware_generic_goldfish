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

#include "hypervisor/hypervisor_service_impl.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/goldfish/vm_interface.h"
#include "fake_vm_operations.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "hypervisor_service.pb.h"
#include "operations/operations_service_impl.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::CreateSnapshotRequest;
using ::android::emulation::v2::DeleteSnapshotRequest;
using ::android::emulation::v2::DeleteSnapshotResponse;
using ::android::emulation::v2::ExportSnapshotRequest;
using ::android::emulation::v2::GetSnapshotRequest;
using ::android::emulation::v2::GetVmStateRequest;
using ::android::emulation::v2::HardResetVmRequest;
using ::android::emulation::v2::ImportSnapshotRequest;
using ::android::emulation::v2::ListSnapshotsRequest;
using ::android::emulation::v2::ListSnapshotsResponse;
using ::android::emulation::v2::PauseVmRequest;
using ::android::emulation::v2::ResetVmRequest;
using ::android::emulation::v2::RestoreSnapshotRequest;
using ::android::emulation::v2::ResumeVmRequest;
using ::android::emulation::v2::SetQuickbootTargetRequest;
using ::android::emulation::v2::SetQuickbootTargetResponse;
using ::android::emulation::v2::Snapshot;
using ::android::emulation::v2::SnapshotOperationMetadata;
using ::android::emulation::v2::StopVmRequest;
using ::android::emulation::v2::StreamVmStateRequest;
using ::android::emulation::v2::UpdateSnapshotRequest;
using ::android::emulation::v2::VmState;
using ::android::goldfish::EmuRunState;
using ::android::goldfish::FakeVmOperations;
using ::android::goldfish::VmOperations;
using ::goldfish::async::testing::TestEventLoop;
using ::google::longrunning::GetOperationRequest;
using ::google::longrunning::ListOperationsRequest;
using ::google::longrunning::ListOperationsResponse;
using ::google::longrunning::Operation;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

class HypervisorServiceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        operations_service_ = std::make_shared<OperationsServiceImpl>();
        hypervisor_service_ =
                std::make_unique<HypervisorServiceImpl>(vm_operations_, operations_service_);
    }

    void TearDown() override {
        hypervisor_service_.reset();
        operations_service_.reset();
    }

    const std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    FakeVmOperations vm_operations_{loop_.get()};
    std::shared_ptr<OperationsServiceImpl> operations_service_;
    std::unique_ptr<HypervisorServiceImpl> hypervisor_service_;
};

TEST_F(HypervisorServiceTest, VmLifecycleStateAndControl) {
    GetVmStateRequest get_req;
    VmState state;
    ServerContext get_ctx;
    ASSERT_TRUE(hypervisor_service_->GetVmState(&get_ctx, &get_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::RUNNING);
    EXPECT_EQ(state.allocated_cpu_core_count(), 4);

    PauseVmRequest pause_req;
    ServerContext pause_ctx;
    ASSERT_TRUE(hypervisor_service_->PauseVm(&pause_ctx, &pause_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::PAUSED);

    // PauseVm is idempotent when already paused.
    ServerContext pause_again_ctx;
    ASSERT_TRUE(hypervisor_service_->PauseVm(&pause_again_ctx, &pause_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::PAUSED);

    ResumeVmRequest resume_req;
    ServerContext resume_ctx;
    ASSERT_TRUE(hypervisor_service_->ResumeVm(&resume_ctx, &resume_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::RUNNING);

    // ResumeVm is idempotent when already running.
    ServerContext resume_again_ctx;
    ASSERT_TRUE(hypervisor_service_->ResumeVm(&resume_again_ctx, &resume_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::RUNNING);

    ResetVmRequest reset_req;
    ServerContext reset_ctx;
    ASSERT_TRUE(hypervisor_service_->ResetVm(&reset_ctx, &reset_req, &state).ok());
    EXPECT_EQ(vm_operations_.reset_count, 1);
    EXPECT_EQ(state.state(), VmState::RUNNING);

    HardResetVmRequest hard_reset_req;
    ServerContext hard_reset_ctx;
    ASSERT_TRUE(hypervisor_service_->HardResetVm(&hard_reset_ctx, &hard_reset_req, &state).ok());
    EXPECT_EQ(vm_operations_.reset_count, 2);
    EXPECT_EQ(state.state(), VmState::RUNNING);

    StopVmRequest stop_req;
    stop_req.set_save_quickboot_snapshot(true);
    ServerContext stop_ctx;
    ASSERT_TRUE(hypervisor_service_->StopVm(&stop_ctx, &stop_req, &state).ok());
    EXPECT_EQ(vm_operations_.shutdown_count, 1);
    EXPECT_EQ(state.state(), VmState::SHUTTING_DOWN);

    // Once shutting down, PauseVm, ResumeVm, ResetVm, and HardResetVm fail with
    // FAILED_PRECONDITION and preserve the underlying error message.
    ServerContext pause_after_stop_ctx;
    const Status pause_status =
            hypervisor_service_->PauseVm(&pause_after_stop_ctx, &pause_req, &state);
    EXPECT_EQ(pause_status.error_code(), StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(absl::StrContains(pause_status.error_message(), "Cannot pause VM"));

    ServerContext resume_after_stop_ctx;
    const Status resume_status =
            hypervisor_service_->ResumeVm(&resume_after_stop_ctx, &resume_req, &state);
    EXPECT_EQ(resume_status.error_code(), StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(absl::StrContains(resume_status.error_message(), "Cannot resume VM"));

    ServerContext reset_after_stop_ctx;
    const Status reset_status =
            hypervisor_service_->ResetVm(&reset_after_stop_ctx, &reset_req, &state);
    EXPECT_EQ(reset_status.error_code(), StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(absl::StrContains(reset_status.error_message(), "Cannot reset VM"));

    ServerContext hard_reset_after_stop_ctx;
    const Status hard_reset_status =
            hypervisor_service_->HardResetVm(&hard_reset_after_stop_ctx, &hard_reset_req, &state);
    EXPECT_EQ(hard_reset_status.error_code(), StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(absl::StrContains(hard_reset_status.error_message(), "Cannot reset VM"));

    // StopVm also propagates FAILED_PRECONDITION when a snapshot is in progress.
    vm_operations_.SetRunState(EmuRunState::SaveVm);
    ServerContext stop_during_snapshot_ctx;
    const Status stop_status =
            hypervisor_service_->StopVm(&stop_during_snapshot_ctx, &stop_req, &state);
    EXPECT_EQ(stop_status.error_code(), StatusCode::FAILED_PRECONDITION);
    EXPECT_TRUE(
            absl::StrContains(stop_status.error_message(), "Cannot shut down VM during snapshot"));
}

TEST_F(HypervisorServiceTest, StopVmRejectsUnsupportedMode) {
    StopVmRequest stop_req;
    stop_req.set_mode(static_cast<StopVmRequest::Mode>(99));
    VmState state;
    ServerContext stop_ctx;
    const Status status = hypervisor_service_->StopVm(&stop_ctx, &stop_req, &state);
    EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(vm_operations_.shutdown_count, 0);
}

TEST_F(HypervisorServiceTest, StopVmTerminateKillsProcessImmediately) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    vm_operations_.SetRunState(EmuRunState::SaveVm);

    StopVmRequest stop_req;
    stop_req.set_mode(StopVmRequest::TERMINATE);
    stop_req.set_reason("Integration test bailout");
    VmState state;
    ServerContext stop_ctx;
    EXPECT_DEATH_IF_SUPPORTED((void)hypervisor_service_->StopVm(&stop_ctx, &stop_req, &state),
                              "Integration test bailout");
}

TEST_F(HypervisorServiceTest, StopVmCrashGeneratesCrashWithReason) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    vm_operations_.SetRunState(EmuRunState::SaveVm);

    StopVmRequest stop_req;
    stop_req.set_mode(StopVmRequest::CRASH);
    stop_req.set_reason("Integration test hang detected");
    VmState state;
    ServerContext stop_ctx;
    EXPECT_DEATH_IF_SUPPORTED((void)hypervisor_service_->StopVm(&stop_ctx, &stop_req, &state),
                              "Integration test hang detected");
}

TEST_F(HypervisorServiceTest, CreateGetUpdateListAndRestoreSnapshot) {
    CreateSnapshotRequest create_req;
    create_req.set_snapshot_id("pre_login");
    create_req.set_display_name("Pre-Login State");
    create_req.set_description("Captured right before login flow");

    Operation create_op;
    ServerContext create_ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&create_ctx, &create_req, &create_op).ok());
    EXPECT_TRUE(create_op.done());

    Snapshot created_snapshot;
    ASSERT_TRUE(create_op.response().UnpackTo(&created_snapshot));
    EXPECT_EQ(created_snapshot.snapshot_id(), "pre_login");
    EXPECT_EQ(created_snapshot.display_name(), "Pre-Login State");
    EXPECT_EQ(created_snapshot.description(), "Captured right before login flow");
    EXPECT_TRUE(created_snapshot.compatibility().host_compatible());

    SnapshotOperationMetadata meta;
    ASSERT_TRUE(create_op.metadata().UnpackTo(&meta));
    EXPECT_EQ(meta.snapshot_id(), "pre_login");
    EXPECT_EQ(meta.progress_percent(), 100);

    // Verify operation is also queryable via OperationsService
    GetOperationRequest get_op_req;
    get_op_req.set_name(create_op.name());
    Operation queried_op;
    ServerContext get_op_ctx;
    ASSERT_TRUE(operations_service_->GetOperation(&get_op_ctx, &get_op_req, &queried_op).ok());
    EXPECT_TRUE(queried_op.done());

    // GetSnapshot
    GetSnapshotRequest get_req;
    get_req.set_snapshot_id("pre_login");
    Snapshot got_snapshot;
    ServerContext get_ctx;
    ASSERT_TRUE(hypervisor_service_->GetSnapshot(&get_ctx, &get_req, &got_snapshot).ok());
    EXPECT_EQ(got_snapshot.snapshot_id(), "pre_login");
    EXPECT_EQ(got_snapshot.display_name(), "Pre-Login State");
    EXPECT_EQ(got_snapshot.description(), "Captured right before login flow");

    // UpdateSnapshot
    UpdateSnapshotRequest update_req;
    update_req.set_snapshot_id("pre_login");
    update_req.set_display_name("Updated Login State");
    Snapshot updated_snapshot;
    ServerContext update_ctx;
    ASSERT_TRUE(
            hypervisor_service_->UpdateSnapshot(&update_ctx, &update_req, &updated_snapshot).ok());
    EXPECT_EQ(updated_snapshot.display_name(), "Updated Login State");
    EXPECT_EQ(updated_snapshot.description(), "Captured right before login flow");

    // ListSnapshots
    ListSnapshotsRequest list_req;
    ListSnapshotsResponse list_resp;
    ServerContext list_ctx;
    ASSERT_TRUE(hypervisor_service_->ListSnapshots(&list_ctx, &list_req, &list_resp).ok());
    ASSERT_EQ(list_resp.snapshots_size(), 1);
    EXPECT_EQ(list_resp.snapshots(0).snapshot_id(), "pre_login");
    EXPECT_EQ(list_resp.snapshots(0).display_name(), "Updated Login State");

    // RestoreSnapshot
    RestoreSnapshotRequest restore_req;
    restore_req.set_snapshot_id("pre_login");
    Operation restore_op;
    ServerContext restore_ctx;
    ASSERT_TRUE(hypervisor_service_->RestoreSnapshot(&restore_ctx, &restore_req, &restore_op).ok());
    EXPECT_TRUE(restore_op.done());
    EXPECT_EQ(vm_operations_.last_loaded_snapshot, "pre_login");
}

TEST_F(HypervisorServiceTest, SetAndClearQuickbootTarget) {
    CreateSnapshotRequest create_req;
    create_req.set_snapshot_id("default_boot");
    Operation create_op;
    ServerContext create_ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&create_ctx, &create_req, &create_op).ok());

    SetQuickbootTargetRequest set_req;
    set_req.set_snapshot_id("default_boot");
    SetQuickbootTargetResponse set_resp;
    ServerContext set_ctx;
    ASSERT_TRUE(hypervisor_service_->SetQuickbootTarget(&set_ctx, &set_req, &set_resp).ok());

    GetSnapshotRequest get_req;
    get_req.set_snapshot_id("default_boot");
    Snapshot snap;
    ServerContext get_ctx;
    ASSERT_TRUE(hypervisor_service_->GetSnapshot(&get_ctx, &get_req, &snap).ok());
    EXPECT_TRUE(snap.quickboot_target());

    // Clear quickboot target
    SetQuickbootTargetRequest clear_req;
    ServerContext clear_ctx;
    ASSERT_TRUE(hypervisor_service_->SetQuickbootTarget(&clear_ctx, &clear_req, &set_resp).ok());

    ServerContext get_ctx2;
    ASSERT_TRUE(hypervisor_service_->GetSnapshot(&get_ctx2, &get_req, &snap).ok());
    EXPECT_FALSE(snap.quickboot_target());
}

TEST_F(HypervisorServiceTest, AutoGeneratesSnapshotIdWhenOmitted) {
    CreateSnapshotRequest create_req;
    create_req.set_display_name("Quick Checkpoint");
    Operation create_op;
    ServerContext create_ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&create_ctx, &create_req, &create_op).ok());
    EXPECT_TRUE(create_op.done());

    Snapshot created_snapshot;
    ASSERT_TRUE(create_op.response().UnpackTo(&created_snapshot));
    EXPECT_FALSE(created_snapshot.snapshot_id().empty());
    EXPECT_TRUE(absl::StartsWith(created_snapshot.snapshot_id(), "snap_"));
    EXPECT_EQ(created_snapshot.display_name(), "Quick Checkpoint");

    SnapshotOperationMetadata meta;
    ASSERT_TRUE(create_op.metadata().UnpackTo(&meta));
    EXPECT_EQ(meta.snapshot_id(), created_snapshot.snapshot_id());

    GetSnapshotRequest get_req;
    get_req.set_snapshot_id(created_snapshot.snapshot_id());
    Snapshot got_snapshot;
    ServerContext get_ctx;
    ASSERT_TRUE(hypervisor_service_->GetSnapshot(&get_ctx, &get_req, &got_snapshot).ok());
    EXPECT_EQ(got_snapshot.snapshot_id(), created_snapshot.snapshot_id());
    EXPECT_EQ(got_snapshot.display_name(), "Quick Checkpoint");
}

TEST_F(HypervisorServiceTest, DeleteSnapshotAndValidationErrors) {
    CreateSnapshotRequest invalid_req;
    invalid_req.set_snapshot_id("../escape");
    Operation op;
    ServerContext ctx;
    EXPECT_EQ(hypervisor_service_->CreateSnapshot(&ctx, &invalid_req, &op).error_code(),
              StatusCode::INVALID_ARGUMENT);

    GetSnapshotRequest missing_req;
    missing_req.set_snapshot_id("nonexistent");
    Snapshot snap;
    ServerContext missing_ctx;
    EXPECT_EQ(hypervisor_service_->GetSnapshot(&missing_ctx, &missing_req, &snap).error_code(),
              StatusCode::NOT_FOUND);

    DeleteSnapshotRequest delete_invalid;
    delete_invalid.set_snapshot_id("../escape");
    DeleteSnapshotResponse del_invalid_resp;
    ServerContext del_invalid_ctx;
    EXPECT_EQ(hypervisor_service_
                      ->DeleteSnapshot(&del_invalid_ctx, &delete_invalid, &del_invalid_resp)
                      .error_code(),
              StatusCode::INVALID_ARGUMENT);

    DeleteSnapshotRequest delete_missing;
    delete_missing.set_snapshot_id("nonexistent");
    DeleteSnapshotResponse del_resp;
    ServerContext del_ctx;
    EXPECT_EQ(
            hypervisor_service_->DeleteSnapshot(&del_ctx, &delete_missing, &del_resp).error_code(),
            StatusCode::NOT_FOUND);
}

TEST_F(HypervisorServiceTest, CreateSnapshotFailureIsReportedInOperation) {
    vm_operations_.fail_save = true;

    CreateSnapshotRequest req;
    req.set_snapshot_id("doomed");
    Operation op;
    ServerContext ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&ctx, &req, &op).ok());

    EXPECT_TRUE(op.done());
    ASSERT_TRUE(op.has_error());
    EXPECT_EQ(op.error().code(), static_cast<int>(absl::StatusCode::kInternal));
    EXPECT_FALSE(op.has_response());
}

TEST_F(HypervisorServiceTest, RestoreSnapshotFailureIsReportedInOperation) {
    CreateSnapshotRequest create_req;
    create_req.set_snapshot_id("restorable");
    Operation create_op;
    ServerContext create_ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&create_ctx, &create_req, &create_op).ok());

    vm_operations_.fail_load = true;

    RestoreSnapshotRequest restore_req;
    restore_req.set_snapshot_id("restorable");
    Operation restore_op;
    ServerContext restore_ctx;
    ASSERT_TRUE(hypervisor_service_->RestoreSnapshot(&restore_ctx, &restore_req, &restore_op).ok());

    EXPECT_TRUE(restore_op.done());
    ASSERT_TRUE(restore_op.has_error());
    EXPECT_EQ(restore_op.error().code(), static_cast<int>(absl::StatusCode::kInternal));
    EXPECT_FALSE(restore_op.has_response());
}

TEST_F(HypervisorServiceTest, ReportsRealTimeProgressDuringInFlightCreateAndRestore) {
    SnapshotOperationMetadata mid_save_meta;
    vm_operations_.on_save = [&](std::string_view name) {
        vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
            .operation = VmOperations::SnapshotOperationType::Save,
            .phase = VmOperations::SnapshotPhase::Preparing,
            .snapshot_id = std::string(name),
            .total_bytes = 4LL * 1024 * 1024 * 1024,
        });
        vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
            .operation = VmOperations::SnapshotOperationType::Save,
            .phase = VmOperations::SnapshotPhase::Transferring,
            .snapshot_id = std::string(name),
            .active_component = "0000:00:02.0/virtio-gpu-rutabaga",
            .transferred_bytes = 2LL * 1024 * 1024 * 1024,
            .total_bytes = 4LL * 1024 * 1024 * 1024,
            .disk_bytes = 512LL * 1024 * 1024,
            .completed_devices = 5,
            .total_devices = 10,
        });

        ListOperationsRequest list_req;
        list_req.set_name("operations/snapshot/create");
        ListOperationsResponse list_resp;
        ServerContext list_ctx;
        ASSERT_TRUE(operations_service_->ListOperations(&list_ctx, &list_req, &list_resp).ok());
        ASSERT_EQ(list_resp.operations_size(), 1);
        EXPECT_FALSE(list_resp.operations(0).done());
        ASSERT_TRUE(list_resp.operations(0).metadata().UnpackTo(&mid_save_meta));

        vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
            .operation = VmOperations::SnapshotOperationType::Save,
            .phase = VmOperations::SnapshotPhase::Completed,
            .snapshot_id = std::string(name),
            .transferred_bytes = 4LL * 1024 * 1024 * 1024,
            .total_bytes = 4LL * 1024 * 1024 * 1024,
            .disk_bytes = 1LL * 1024 * 1024 * 1024,
            .completed_devices = 10,
            .total_devices = 10,
        });
    };

    CreateSnapshotRequest create_req;
    create_req.set_snapshot_id("progress_snap");
    Operation create_op;
    ServerContext create_ctx;
    ASSERT_TRUE(hypervisor_service_->CreateSnapshot(&create_ctx, &create_req, &create_op).ok());
    EXPECT_TRUE(create_op.done());

    // Drain any queued loop events and ensure no duplicate implicit LRO was created.
    loop_->RunAll();
    {
        ListOperationsRequest list_req;
        list_req.set_name("operations/snapshot/create");
        ListOperationsResponse list_resp;
        ServerContext list_ctx;
        ASSERT_TRUE(operations_service_->ListOperations(&list_ctx, &list_req, &list_resp).ok());
        EXPECT_EQ(list_resp.operations_size(), 1);
    }

    EXPECT_EQ(mid_save_meta.snapshot_id(), "progress_snap");
    EXPECT_EQ(mid_save_meta.phase(), SnapshotOperationMetadata::TRANSFERRING);
    EXPECT_EQ(mid_save_meta.progress_percent(), 50);
    EXPECT_EQ(mid_save_meta.active_component(), "0000:00:02.0/virtio-gpu-rutabaga");
    EXPECT_EQ(mid_save_meta.completed_devices(), 5);
    EXPECT_EQ(mid_save_meta.total_devices(), 10);
    EXPECT_EQ(mid_save_meta.transferred_bytes(), 2LL * 1024 * 1024 * 1024);
    EXPECT_EQ(mid_save_meta.total_bytes(), 4LL * 1024 * 1024 * 1024);

    SnapshotOperationMetadata final_save_meta;
    ASSERT_TRUE(create_op.metadata().UnpackTo(&final_save_meta));
    EXPECT_EQ(final_save_meta.progress_percent(), 100);
    EXPECT_TRUE(final_save_meta.active_component().empty());
    EXPECT_EQ(final_save_meta.completed_devices(), 10);
    EXPECT_EQ(final_save_meta.total_devices(), 10);
    EXPECT_EQ(final_save_meta.transferred_bytes(), 4LL * 1024 * 1024 * 1024);
    EXPECT_EQ(final_save_meta.total_bytes(), 4LL * 1024 * 1024 * 1024);

    Snapshot created_snap;
    ASSERT_TRUE(create_op.response().UnpackTo(&created_snap));
    EXPECT_EQ(created_snap.size_bytes(), 1LL * 1024 * 1024 * 1024);

    SnapshotOperationMetadata mid_load_meta;
    vm_operations_.on_load = [&](std::string_view name) {
        vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
            .operation = VmOperations::SnapshotOperationType::Load,
            .phase = VmOperations::SnapshotPhase::Transferring,
            .snapshot_id = std::string(name),
            .active_component = "ram",
            .transferred_bytes = 750LL * 1024 * 1024,
            .total_bytes = 1000LL * 1024 * 1024,
            .disk_bytes = 750LL * 1024 * 1024,
        });

        ListOperationsRequest list_req;
        list_req.set_name("operations/snapshot/restore");
        ListOperationsResponse list_resp;
        ServerContext list_ctx;
        ASSERT_TRUE(operations_service_->ListOperations(&list_ctx, &list_req, &list_resp).ok());
        ASSERT_EQ(list_resp.operations_size(), 1);
        EXPECT_FALSE(list_resp.operations(0).done());
        ASSERT_TRUE(list_resp.operations(0).metadata().UnpackTo(&mid_load_meta));

        vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
            .operation = VmOperations::SnapshotOperationType::Load,
            .phase = VmOperations::SnapshotPhase::Completed,
            .snapshot_id = std::string(name),
            .transferred_bytes = 1000LL * 1024 * 1024,
            .total_bytes = 1000LL * 1024 * 1024,
            .disk_bytes = 750LL * 1024 * 1024,
        });
    };

    RestoreSnapshotRequest restore_req;
    restore_req.set_snapshot_id("progress_snap");
    Operation restore_op;
    ServerContext restore_ctx;
    ASSERT_TRUE(hypervisor_service_->RestoreSnapshot(&restore_ctx, &restore_req, &restore_op).ok());
    EXPECT_TRUE(restore_op.done());
    loop_->RunAll();

    EXPECT_EQ(mid_load_meta.snapshot_id(), "progress_snap");
    EXPECT_EQ(mid_load_meta.phase(), SnapshotOperationMetadata::TRANSFERRING);
    EXPECT_EQ(mid_load_meta.progress_percent(), 75);
    EXPECT_EQ(mid_load_meta.active_component(), "ram");
    EXPECT_EQ(mid_load_meta.transferred_bytes(), 750LL * 1024 * 1024);
    EXPECT_EQ(mid_load_meta.total_bytes(), 1000LL * 1024 * 1024);

    SnapshotOperationMetadata final_load_meta;
    ASSERT_TRUE(restore_op.metadata().UnpackTo(&final_load_meta));
    EXPECT_EQ(final_load_meta.progress_percent(), 100);
    EXPECT_EQ(final_load_meta.transferred_bytes(), 1000LL * 1024 * 1024);
    EXPECT_EQ(final_load_meta.total_bytes(), 1000LL * 1024 * 1024);
}

TEST_F(HypervisorServiceTest, TracksImplicitHypervisorSnapshotProgressAndExitSaveState) {
    // 1. Simulate an implicit boot-time try_loadvm("default_boot") that fails mid-stream.
    vm_operations_.run_state = EmuRunState::RestoreVm;
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Load,
        .phase = VmOperations::SnapshotPhase::Preparing,
        .snapshot_id = "default_boot",
        .transferred_bytes = 0,
        .total_bytes = 2LL * 1024 * 1024 * 1024,
    });
    loop_->RunAll();

    GetVmStateRequest state_req;
    VmState load_vm_state;
    ServerContext load_state_ctx;
    ASSERT_TRUE(hypervisor_service_->GetVmState(&load_state_ctx, &state_req, &load_vm_state).ok());
    EXPECT_EQ(load_vm_state.state(), VmState::RESTORING_SNAPSHOT);
    ASSERT_TRUE(load_vm_state.has_snapshot_progress());
    EXPECT_EQ(load_vm_state.snapshot_progress().snapshot_id(), "default_boot");

    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Load,
        .phase = VmOperations::SnapshotPhase::Failed,
        .snapshot_id = "default_boot",
        .transferred_bytes = 512LL * 1024 * 1024,
        .total_bytes = 2LL * 1024 * 1024 * 1024,
        .error_message = "Error while reading VM state",
    });
    loop_->RunAll();

    VmState post_fail_vm_state;
    ServerContext post_fail_state_ctx;
    ASSERT_TRUE(
            hypervisor_service_->GetVmState(&post_fail_state_ctx, &state_req, &post_fail_vm_state)
                    .ok());
    EXPECT_EQ(post_fail_vm_state.state(), VmState::RUNNING);
    EXPECT_FALSE(post_fail_vm_state.has_snapshot_progress());
    vm_operations_.run_state = EmuRunState::Running;

    ListOperationsRequest restore_list_req;
    restore_list_req.set_name("operations/snapshot/restore");
    ListOperationsResponse restore_list_resp;
    ServerContext restore_list_ctx;
    ASSERT_TRUE(operations_service_
                        ->ListOperations(&restore_list_ctx, &restore_list_req, &restore_list_resp)
                        .ok());
    ASSERT_EQ(restore_list_resp.operations_size(), 1);
    EXPECT_TRUE(restore_list_resp.operations(0).done());
    EXPECT_TRUE(restore_list_resp.operations(0).has_error());
    EXPECT_EQ(restore_list_resp.operations(0).error().message(), "Error while reading VM state");

    // 2. Request VM shutdown via StopVm, then simulate QEMU entering try_savevm("default_boot").
    // Per proto contract, VmState.state must remain SHUTTING_DOWN (not SAVING_SNAPSHOT) while
    // VmState.snapshot_progress is populated during the quickboot exit-save.
    StopVmRequest stop_req;
    stop_req.set_save_quickboot_snapshot(true);
    VmState stop_state;
    ServerContext stop_ctx;
    ASSERT_TRUE(hypervisor_service_->StopVm(&stop_ctx, &stop_req, &stop_state).ok());
    EXPECT_EQ(stop_state.state(), VmState::SHUTTING_DOWN);

    vm_operations_.run_state = EmuRunState::SaveVm;
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Preparing,
        .snapshot_id = "default_boot",
        .transferred_bytes = 0,
        .total_bytes = 8LL * 1024 * 1024 * 1024,
    });
    loop_->RunAll();

    VmState prep_vm_state;
    ServerContext prep_state_ctx;
    ASSERT_TRUE(hypervisor_service_->GetVmState(&prep_state_ctx, &state_req, &prep_vm_state).ok());
    EXPECT_EQ(prep_vm_state.state(), VmState::SHUTTING_DOWN);
    ASSERT_TRUE(prep_vm_state.has_snapshot_progress());
    EXPECT_EQ(prep_vm_state.snapshot_progress().snapshot_id(), "default_boot");
    EXPECT_EQ(prep_vm_state.snapshot_progress().phase(), SnapshotOperationMetadata::PREPARING);

    // 3. Transition to TRANSFERRING and advance bytes to 50%.
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Transferring,
        .snapshot_id = "default_boot",
        .active_component = "ram",
        .transferred_bytes = 4LL * 1024 * 1024 * 1024,
        .total_bytes = 8LL * 1024 * 1024 * 1024,
    });
    loop_->RunAll();

    VmState mid_vm_state;
    ServerContext mid_state_ctx;
    ASSERT_TRUE(hypervisor_service_->GetVmState(&mid_state_ctx, &state_req, &mid_vm_state).ok());
    EXPECT_EQ(mid_vm_state.state(), VmState::SHUTTING_DOWN);
    ASSERT_TRUE(mid_vm_state.has_snapshot_progress());
    EXPECT_EQ(mid_vm_state.snapshot_progress().progress_percent(), 50);
    EXPECT_EQ(mid_vm_state.snapshot_progress().active_component(), "ram");

    ListOperationsRequest list_req;
    list_req.set_name("operations/snapshot/create");
    ListOperationsResponse list_resp;
    ServerContext list_ctx;
    ASSERT_TRUE(operations_service_->ListOperations(&list_ctx, &list_req, &list_resp).ok());
    ASSERT_EQ(list_resp.operations_size(), 1);
    EXPECT_FALSE(list_resp.operations(0).done());

    SnapshotOperationMetadata live_meta;
    ASSERT_TRUE(list_resp.operations(0).metadata().UnpackTo(&live_meta));
    EXPECT_EQ(live_meta.snapshot_id(), "default_boot");
    EXPECT_EQ(live_meta.phase(), SnapshotOperationMetadata::TRANSFERRING);
    EXPECT_EQ(live_meta.progress_percent(), 50);
    EXPECT_EQ(live_meta.active_component(), "ram");
    EXPECT_EQ(live_meta.transferred_bytes(), 4LL * 1024 * 1024 * 1024);
    EXPECT_EQ(live_meta.total_bytes(), 8LL * 1024 * 1024 * 1024);

    // 4. Simulate save_snapshot() completing and briefly calling vm_resume(Running) before exit.
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Completed,
        .snapshot_id = "default_boot",
        .transferred_bytes = 8LL * 1024 * 1024 * 1024,
        .total_bytes = 8LL * 1024 * 1024 * 1024,
        .disk_bytes = 2LL * 1024 * 1024 * 1024,
    });
    vm_operations_.SetRunState(EmuRunState::Running);
    loop_->RunAll();

    VmState post_save_vm_state;
    ServerContext post_save_state_ctx;
    ASSERT_TRUE(
            hypervisor_service_->GetVmState(&post_save_state_ctx, &state_req, &post_save_vm_state)
                    .ok());
    EXPECT_EQ(post_save_vm_state.state(), VmState::SHUTTING_DOWN);
    EXPECT_FALSE(post_save_vm_state.has_snapshot_progress());

    GetOperationRequest get_req;
    get_req.set_name(list_resp.operations(0).name());
    Operation completed_op;
    ServerContext get_ctx;
    ASSERT_TRUE(operations_service_->GetOperation(&get_ctx, &get_req, &completed_op).ok());
    EXPECT_TRUE(completed_op.done());
    EXPECT_TRUE(completed_op.has_response());

    SnapshotOperationMetadata final_meta;
    ASSERT_TRUE(completed_op.metadata().UnpackTo(&final_meta));
    EXPECT_EQ(final_meta.progress_percent(), 100);
    EXPECT_EQ(final_meta.transferred_bytes(), 8LL * 1024 * 1024 * 1024);
}

TEST_F(HypervisorServiceTest, StreamVmStateDeliversEverySnapshotPhaseFromEventPayload) {
    ::grpc::ServerBuilder builder;
    builder.RegisterService(hypervisor_service_.get());
    std::unique_ptr<::grpc::Server> server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);
    auto stub = ::android::emulation::v2::HypervisorService::NewStub(
            server->InProcessChannel(::grpc::ChannelArguments()));

    ::grpc::ClientContext stream_ctx;
    stream_ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
    StreamVmStateRequest stream_req;
    auto reader = stub->StreamVmState(&stream_ctx, stream_req);

    // 1. Initial state message on connection.
    VmState msg;
    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::RUNNING);
    EXPECT_FALSE(msg.has_snapshot_progress());

    // 2. Enqueue PREPARING, TRANSFERRING, and COMPLETED onto the event loop before draining it,
    // so GetSnapshotProgress() is already Completed when the loop runs. StreamVmState must
    // still emit PREPARING and TRANSFERRING from the event payloads without skipping phases.
    vm_operations_.SetRunState(EmuRunState::SaveVm);
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Preparing,
        .snapshot_id = "fast_snap",
        .total_bytes = 1000,
    });
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Transferring,
        .snapshot_id = "fast_snap",
        .active_component = "ram",
        .transferred_bytes = 500,
        .total_bytes = 1000,
    });
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Completed,
        .snapshot_id = "fast_snap",
        .transferred_bytes = 1000,
        .total_bytes = 1000,
    });
    vm_operations_.SetRunState(EmuRunState::Running);
    loop_->RunAll();

    // Expect PREPARING -> TRANSFERRING -> RUNNING (without duplicate RUNNING).
    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SAVING_SNAPSHOT);
    ASSERT_TRUE(msg.has_snapshot_progress());
    EXPECT_EQ(msg.snapshot_progress().phase(), SnapshotOperationMetadata::PREPARING);
    EXPECT_EQ(msg.snapshot_progress().progress_percent(), 0);

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SAVING_SNAPSHOT);
    ASSERT_TRUE(msg.has_snapshot_progress());
    EXPECT_EQ(msg.snapshot_progress().phase(), SnapshotOperationMetadata::TRANSFERRING);
    EXPECT_EQ(msg.snapshot_progress().progress_percent(), 50);
    EXPECT_EQ(msg.snapshot_progress().active_component(), "ram");

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::RUNNING);
    EXPECT_FALSE(msg.has_snapshot_progress());

    // 3. Request StopVm and enqueue exit-save PREPARING, TRANSFERRING, COMPLETED, and a post-save
    // Running notification before flushing the loop with WaitUntilIdle() (matching
    // grpc_main_loop_exit_notify). StreamVmState must deliver SHUTTING_DOWN, each exit-save phase
    // under SHUTTING_DOWN, and the final SHUTTING_DOWN state with snapshot_progress cleared.
    StopVmRequest stop_req;
    stop_req.set_save_quickboot_snapshot(true);
    VmState stop_state;
    ServerContext stop_ctx;
    ASSERT_TRUE(hypervisor_service_->StopVm(&stop_ctx, &stop_req, &stop_state).ok());

    vm_operations_.SetRunState(EmuRunState::SaveVm);
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Preparing,
        .snapshot_id = "default_boot",
        .total_bytes = 2000,
    });
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Transferring,
        .snapshot_id = "default_boot",
        .active_component = "ram",
        .transferred_bytes = 1000,
        .total_bytes = 2000,
    });
    vm_operations_.EmitSnapshotProgress(VmOperations::SnapshotProgress{
        .operation = VmOperations::SnapshotOperationType::Save,
        .phase = VmOperations::SnapshotPhase::Completed,
        .snapshot_id = "default_boot",
        .transferred_bytes = 2000,
        .total_bytes = 2000,
    });
    vm_operations_.SetRunState(EmuRunState::Running);
    loop_->RunAll();

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SHUTTING_DOWN);
    EXPECT_FALSE(msg.has_snapshot_progress());

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SHUTTING_DOWN);
    ASSERT_TRUE(msg.has_snapshot_progress());
    EXPECT_EQ(msg.snapshot_progress().phase(), SnapshotOperationMetadata::PREPARING);
    EXPECT_EQ(msg.snapshot_progress().snapshot_id(), "default_boot");

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SHUTTING_DOWN);
    ASSERT_TRUE(msg.has_snapshot_progress());
    EXPECT_EQ(msg.snapshot_progress().phase(), SnapshotOperationMetadata::TRANSFERRING);
    EXPECT_EQ(msg.snapshot_progress().progress_percent(), 50);

    ASSERT_TRUE(reader->Read(&msg));
    EXPECT_EQ(msg.state(), VmState::SHUTTING_DOWN);
    EXPECT_FALSE(msg.has_snapshot_progress());

    stream_ctx.TryCancel();
    EXPECT_EQ(reader->Finish().error_code(), StatusCode::CANCELLED);
    server->Shutdown();
    server->Wait();
}

}  // namespace
}  // namespace goldfish::grpc::v2
