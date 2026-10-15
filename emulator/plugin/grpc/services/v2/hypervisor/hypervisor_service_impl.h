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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"

#include "android/goldfish/vm_interface.h"
#include "hypervisor_service.grpc.pb.h"
#include "operations/operations_service_impl.h"

namespace goldfish::grpc::v2 {

using HypervisorServiceBase =
        ::android::emulation::v2::HypervisorService::WithCallbackMethod_StreamVmState<
                ::android::emulation::v2::HypervisorService::Service>;

/**
 * gRPC service implementing android.emulation.v2.HypervisorService.
 *
 * Bridges the v2 hypervisor execution state RPCs (get, stream, pause, resume, reset, hard reset,
 * stop) and snapshot lifecycle RPCs (list, get, update, create, restore, delete, quickboot target)
 * onto the hypervisor `VmOperations` interface while tracking long-running operation state via
 * `OperationsServiceImpl`.
 *
 * Thread Safety:
 *   - All public RPC methods are thread-safe. In-memory metadata (`user_metadata_` and
 *     `quickboot_target_id_`) is synchronized via `state_mutex_`, and hypervisor calls are
 *     executed outside of `state_mutex_` to prevent lock inversion.
 */
class HypervisorServiceImpl final : public HypervisorServiceBase {
  public:
    struct UserSnapshotMetadata {
        std::string display_name;
        std::string description;
    };

    HypervisorServiceImpl(::android::goldfish::VmOperations& vm_operations,
                          std::shared_ptr<OperationsServiceImpl> operations_service);
    ~HypervisorServiceImpl() override;

    HypervisorServiceImpl(const HypervisorServiceImpl&) = delete;
    HypervisorServiceImpl& operator=(const HypervisorServiceImpl&) = delete;

    ::grpc::Status GetVmState(::grpc::ServerContext* context,
                              const ::android::emulation::v2::GetVmStateRequest* request,
                              ::android::emulation::v2::VmState* response) override;

    ::grpc::ServerWriteReactor<::android::emulation::v2::VmState>* StreamVmState(
            ::grpc::CallbackServerContext* context,
            const ::android::emulation::v2::StreamVmStateRequest* request) override;

    ::grpc::Status PauseVm(::grpc::ServerContext* context,
                           const ::android::emulation::v2::PauseVmRequest* request,
                           ::android::emulation::v2::VmState* response) override;

    ::grpc::Status ResumeVm(::grpc::ServerContext* context,
                            const ::android::emulation::v2::ResumeVmRequest* request,
                            ::android::emulation::v2::VmState* response) override;

    ::grpc::Status ResetVm(::grpc::ServerContext* context,
                           const ::android::emulation::v2::ResetVmRequest* request,
                           ::android::emulation::v2::VmState* response) override;

    ::grpc::Status HardResetVm(::grpc::ServerContext* context,
                               const ::android::emulation::v2::HardResetVmRequest* request,
                               ::android::emulation::v2::VmState* response) override;

    ::grpc::Status StopVm(::grpc::ServerContext* context,
                          const ::android::emulation::v2::StopVmRequest* request,
                          ::android::emulation::v2::VmState* response) override;

    ::grpc::Status ListSnapshots(::grpc::ServerContext* context,
                                 const ::android::emulation::v2::ListSnapshotsRequest* request,
                                 ::android::emulation::v2::ListSnapshotsResponse* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status GetSnapshot(::grpc::ServerContext* context,
                               const ::android::emulation::v2::GetSnapshotRequest* request,
                               ::android::emulation::v2::Snapshot* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status UpdateSnapshot(::grpc::ServerContext* context,
                                  const ::android::emulation::v2::UpdateSnapshotRequest* request,
                                  ::android::emulation::v2::Snapshot* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status CreateSnapshot(::grpc::ServerContext* context,
                                  const ::android::emulation::v2::CreateSnapshotRequest* request,
                                  ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status RestoreSnapshot(::grpc::ServerContext* context,
                                   const ::android::emulation::v2::RestoreSnapshotRequest* request,
                                   ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status DeleteSnapshot(::grpc::ServerContext* context,
                                  const ::android::emulation::v2::DeleteSnapshotRequest* request,
                                  ::android::emulation::v2::DeleteSnapshotResponse* response)
            override ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status SetQuickbootTarget(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::SetQuickbootTargetRequest* request,
            ::android::emulation::v2::SetQuickbootTargetResponse* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status ExportSnapshot(::grpc::ServerContext* context,
                                  const ::android::emulation::v2::ExportSnapshotRequest* request,
                                  ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::grpc::Status ImportSnapshot(::grpc::ServerContext* context,
                                  const ::android::emulation::v2::ImportSnapshotRequest* request,
                                  ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(state_mutex_);

  private:
    using SnapshotOperationKey =
            std::pair<::android::goldfish::VmOperations::SnapshotOperationType, std::string>;

    struct TrackedSnapshotOperation {
        std::string lro_name;
        std::string snapshot_id;
        bool is_rpc = false;
    };

    void PopulateVmState(::android::emulation::v2::VmState* state) const;
    ::grpc::Status ResetVmInternal(::android::emulation::v2::VmState* response);
    ::grpc::Status FindSnapshotEntry(std::string_view snapshot_id,
                                     ::android::goldfish::VmOperations::SnapshotEntry* out_entry) const;
    void BuildVmState(::android::goldfish::EmuRunState run_state,
                      const ::android::goldfish::VmOperations::SnapshotProgress& snap_progress,
                      ::android::emulation::v2::VmState* state) const;
    void PopulateSnapshotProto(const ::android::goldfish::VmOperations::SnapshotEntry& entry,
                               ::android::emulation::v2::Snapshot* out) const
            ABSL_LOCKS_EXCLUDED(state_mutex_);
    void RefreshOperationMetadata(::google::longrunning::Operation* op) const;
    void OnRunStateChanged(::android::goldfish::EmuRunState state);
    void OnSnapshotStateChanged(const ::android::goldfish::VmOperations::SnapshotProgress& progress)
            ABSL_LOCKS_EXCLUDED(state_mutex_);

    ::android::goldfish::VmOperations& vm_operations_;
    std::shared_ptr<OperationsServiceImpl> operations_service_;
    mutable ::android::base::eventing::CallbackEventSource<::android::emulation::v2::VmState>
            vm_state_events_;
    mutable std::atomic<bool> shutting_down_{false};

    mutable absl::Mutex state_mutex_;
    std::string quickboot_target_id_ ABSL_GUARDED_BY(state_mutex_);
    absl::flat_hash_map<std::string, UserSnapshotMetadata> user_metadata_
            ABSL_GUARDED_BY(state_mutex_);
    absl::flat_hash_map<SnapshotOperationKey, TrackedSnapshotOperation> snapshot_operations_
            ABSL_GUARDED_BY(state_mutex_);
    ::android::goldfish::VmOperations::RunStateSource::CallbackId run_state_callback_id_{};
    ::android::goldfish::VmOperations::SnapshotSource::CallbackId snapshot_callback_id_{};
};

}  // namespace goldfish::grpc::v2
