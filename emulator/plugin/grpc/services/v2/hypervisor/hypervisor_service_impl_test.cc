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

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/time/time.h"

#include "android/goldfish/vm_interface.h"
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
using ::android::emulation::v2::UpdateSnapshotRequest;
using ::android::emulation::v2::VmState;
using ::android::goldfish::EmuRunState;
using ::android::goldfish::QemuShutdownCause;
using ::android::goldfish::VmConfiguration;
using ::android::goldfish::VmHypervisorType;
using ::android::goldfish::VmOperations;
using ::goldfish::async::testing::TestEventLoop;
using ::google::longrunning::GetOperationRequest;
using ::google::longrunning::Operation;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

class FakeVmOperations final : public VmOperations {
  public:
    explicit FakeVmOperations(::goldfish::async::EventLoop* loop) : VmOperations(loop) {}

    bool Stop() override { return true; }
    bool Start() override { return true; }
    void Reset() override {
        ++reset_count_;
        run_state_ = EmuRunState::Running;
    }
    void Shutdown() override {
        ++shutdown_count_;
        run_state_ = EmuRunState::Shutdown;
    }
    bool Pause() override {
        run_state_ = EmuRunState::Paused;
        return true;
    }
    bool Resume() override {
        run_state_ = EmuRunState::Running;
        return true;
    }
    bool IsRunning() override { return run_state_ == EmuRunState::Running; }
    VmConfiguration GetConfiguration() override {
        return {
            .hypervisor_type = VmHypervisorType::Hvf,
            .number_of_cpu_cores = 4,
            .cpu_type = "arm",
        };
    }
    EmuRunState GetRunState() override { return run_state_; }
    void SystemShutdownRequest(QemuShutdownCause) override {}
    void LoadSnapshotResume(EmuRunState) override {}

    absl::Status SaveSnapshot(const char* name, bool /*overwrite*/) override {
        if (fail_save_) {
            return absl::InternalError("Failed to write snapshot to disk");
        }
        entries_[name] = SnapshotEntry{
            .id = name,
            .name = name,
            .timestamp = absl::FromUnixSeconds(1700000000),
        };
        return absl::OkStatus();
    }

    absl::Status LoadSnapshot(const char* id_or_name, bool /*and_resume*/) override {
        if (fail_load_) {
            return absl::InternalError("Failed to read snapshot from disk");
        }
        if (!entries_.contains(id_or_name)) {
            return absl::NotFoundError("Snapshot does not exist");
        }
        last_loaded_snapshot_ = id_or_name;
        return absl::OkStatus();
    }

    absl::Status DeleteSnapshot(const char* id_or_name) override {
        if (entries_.erase(id_or_name) == 0) {
            return absl::NotFoundError("Snapshot does not exist");
        }
        return absl::OkStatus();
    }

    absl::Status ListSnapshots(SnapshotEntrySink sink) override {
        for (const auto& [id, entry] : entries_) {
            sink(entry);
        }
        return absl::OkStatus();
    }

    bool fail_save_ = false;
    bool fail_load_ = false;
    int reset_count_ = 0;
    int shutdown_count_ = 0;
    EmuRunState run_state_ = EmuRunState::Running;
    std::string last_loaded_snapshot_;
    absl::flat_hash_map<std::string, SnapshotEntry> entries_;
};

class HypervisorServiceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        operations_service_ = std::make_shared<OperationsServiceImpl>();
        hypervisor_service_ =
                std::make_unique<HypervisorServiceImpl>(vm_operations_, operations_service_);
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

    ResumeVmRequest resume_req;
    ServerContext resume_ctx;
    ASSERT_TRUE(hypervisor_service_->ResumeVm(&resume_ctx, &resume_req, &state).ok());
    EXPECT_EQ(state.state(), VmState::RUNNING);

    ResetVmRequest reset_req;
    ServerContext reset_ctx;
    ASSERT_TRUE(hypervisor_service_->ResetVm(&reset_ctx, &reset_req, &state).ok());
    EXPECT_EQ(vm_operations_.reset_count_, 1);
    EXPECT_EQ(state.state(), VmState::RUNNING);

    StopVmRequest stop_req;
    stop_req.set_save_quickboot_snapshot(true);
    ServerContext stop_ctx;
    ASSERT_TRUE(hypervisor_service_->StopVm(&stop_ctx, &stop_req, &state).ok());
    EXPECT_EQ(vm_operations_.shutdown_count_, 1);
    EXPECT_EQ(state.state(), VmState::SHUTTING_DOWN);
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
    EXPECT_EQ(vm_operations_.last_loaded_snapshot_, "pre_login");
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
    vm_operations_.fail_save_ = true;

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

    vm_operations_.fail_load_ = true;

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

}  // namespace
}  // namespace goldfish::grpc::v2
