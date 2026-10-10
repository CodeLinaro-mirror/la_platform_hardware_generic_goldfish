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

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/timestamp.pb.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/grpc_event_stream_support.h"

namespace goldfish::grpc::v2 {

using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::control::StateStreamWriter;
using ::android::emulation::v2::CompatibilityFingerprint;
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
using ::android::goldfish::VmOperations;
using ::google::longrunning::Operation;
using ::grpc::CallbackServerContext;
using ::grpc::ServerContext;
using ::grpc::ServerWriteReactor;
using ::grpc::Status;
using ::grpc::StatusCode;

namespace {

bool ContainsInvalidPathChars(std::string_view snapshot_id) {
    return absl::StrContains(snapshot_id, "/") || absl::StrContains(snapshot_id, "\\") ||
           absl::StrContains(snapshot_id, "..");
}

std::string GenerateSnapshotId() {
    static std::atomic<uint64_t> sequence{1};
    const uint64_t id = sequence.fetch_add(1, std::memory_order_relaxed);
    return absl::StrCat(
            "snap_", absl::FormatTime("%Y%m%d_%H%M%S", absl::Now(), absl::UTCTimeZone()), "_", id);
}

SnapshotOperationMetadata MakeOperationMetadata(std::string_view snapshot_id,
                                                SnapshotOperationMetadata::Phase phase,
                                                int32_t progress_percent) {
    SnapshotOperationMetadata metadata;
    metadata.set_snapshot_id(std::string(snapshot_id));
    metadata.set_phase(phase);
    metadata.set_progress_percent(progress_percent);
    return metadata;
}

}  // namespace

HypervisorServiceImpl::HypervisorServiceImpl(
        VmOperations& vm_operations, std::shared_ptr<OperationsServiceImpl> operations_service)
        : vm_operations_(vm_operations), operations_service_(std::move(operations_service)) {
    DCHECK(operations_service_ != nullptr) << "OperationsServiceImpl cannot be null.";
}

void HypervisorServiceImpl::PopulateVmState(VmState* state) const {
    const EmuRunState run_state = vm_operations_.getRunState();
    switch (run_state) {
    case EmuRunState::Running:
        state->set_state(VmState::RUNNING);
        break;
    case EmuRunState::Paused:
    case EmuRunState::Suspended:
        state->set_state(VmState::PAUSED);
        break;
    case EmuRunState::Shutdown:
        state->set_state(VmState::SHUTTING_DOWN);
        break;
    case EmuRunState::SaveVm:
        state->set_state(VmState::SAVING_SNAPSHOT);
        break;
    case EmuRunState::RestoreVm:
        state->set_state(VmState::RESTORING_SNAPSHOT);
        break;
    default:
        state->set_state(vm_operations_.isRunning() ? VmState::RUNNING : VmState::STOPPED);
        break;
    }

    const auto config = vm_operations_.getConfiguration();
    state->set_allocated_cpu_core_count(config.numberOfCpuCores > 0 ? config.numberOfCpuCores : 1);
}

Status HypervisorServiceImpl::GetVmState(ServerContext* /*context*/,
                                         const GetVmStateRequest* /*request*/, VmState* response) {
    PopulateVmState(response);
    return Status::OK;
}

ServerWriteReactor<VmState>* HypervisorServiceImpl::StreamVmState(
        CallbackServerContext* /*context*/, const StreamVmStateRequest* /*request*/) {
    return new StateStreamWriter<VmState, EmuRunState, VmOperations>(
            &vm_operations_, [this](VmState* state) { PopulateVmState(state); });
}

Status HypervisorServiceImpl::PauseVm(ServerContext* /*context*/, const PauseVmRequest* /*request*/,
                                      VmState* response) {
    vm_operations_.pause();
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::ResumeVm(ServerContext* /*context*/,
                                       const ResumeVmRequest* /*request*/, VmState* response) {
    vm_operations_.resume();
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::ResetVm(ServerContext* /*context*/, const ResetVmRequest* /*request*/,
                                      VmState* response) {
    vm_operations_.reset();
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::HardResetVm(ServerContext* /*context*/,
                                          const HardResetVmRequest* /*request*/,
                                          VmState* response) {
    vm_operations_.reset();
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::StopVm(ServerContext* /*context*/, const StopVmRequest* request,
                                     VmState* response) {
    if (request != nullptr && request->save_quickboot_snapshot()) {
        LOG(WARNING) << "save_quickboot_snapshot is currently unimplemented; relying on "
                        "QEMU default shutdown lifecycle.";
    }
    vm_operations_.Shutdown();
    PopulateVmState(response);
    return Status::OK;
}

void HypervisorServiceImpl::PopulateSnapshotProto(const VmOperations::SnapshotEntry& entry,
                                                  Snapshot* out) const {
    out->set_snapshot_id(entry.id);

    std::string display_name = entry.name.empty() ? entry.id : entry.name;
    std::string description;
    bool is_quickboot = false;
    {
        absl::MutexLock state_lock(&state_mutex_);
        is_quickboot = (entry.id == quickboot_target_id_);
        if (auto it = user_metadata_.find(entry.id); it != user_metadata_.end()) {
            if (!it->second.display_name.empty()) {
                display_name = it->second.display_name;
            }
            description = it->second.description;
        }
    }

    out->set_display_name(std::move(display_name));
    out->set_description(std::move(description));
    out->set_quickboot_target(is_quickboot);

    const int64_t seconds = absl::ToUnixSeconds(entry.timestamp);
    out->mutable_create_time()->set_seconds(seconds);
    out->mutable_create_time()->set_nanos(static_cast<int32_t>(
            absl::ToInt64Nanoseconds(entry.timestamp - absl::FromUnixSeconds(seconds))));

    CompatibilityFingerprint* compat = out->mutable_compatibility();
    compat->set_host_compatible(true);
}

Status HypervisorServiceImpl::ListSnapshots(ServerContext* /*context*/,
                                            const ListSnapshotsRequest* request,
                                            ListSnapshotsResponse* response) {
    const bool compatible_only = request != nullptr && request->compatible_only();
    const absl::Status status = vm_operations_.ListSnapshots(
            [this, response, compatible_only](VmOperations::SnapshotEntry entry) {
                Snapshot snap;
                PopulateSnapshotProto(entry, &snap);
                if (!compatible_only || snap.compatibility().host_compatible()) {
                    *response->add_snapshots() = std::move(snap);
                }
            });
    return AbslStatusToGrpcStatus(status);
}

Status HypervisorServiceImpl::GetSnapshot(ServerContext* /*context*/,
                                          const GetSnapshotRequest* request, Snapshot* response) {
    if (request->snapshot_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "snapshot_id cannot be empty."};
    }

    std::optional<VmOperations::SnapshotEntry> found;
    const absl::Status status =
            vm_operations_.ListSnapshots([&](VmOperations::SnapshotEntry entry) {
                if (entry.id == request->snapshot_id()) {
                    found = std::move(entry);
                }
            });
    if (!status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    if (!found.has_value()) {
        return {StatusCode::NOT_FOUND,
                absl::StrCat("Snapshot not found: ", request->snapshot_id())};
    }

    PopulateSnapshotProto(*found, response);
    return Status::OK;
}

Status HypervisorServiceImpl::UpdateSnapshot(ServerContext* /*context*/,
                                             const UpdateSnapshotRequest* request,
                                             Snapshot* response) {
    if (request->snapshot_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "snapshot_id cannot be empty."};
    }

    std::optional<VmOperations::SnapshotEntry> found;
    const absl::Status status =
            vm_operations_.ListSnapshots([&](VmOperations::SnapshotEntry entry) {
                if (entry.id == request->snapshot_id()) {
                    found = std::move(entry);
                }
            });
    if (!status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    if (!found.has_value()) {
        return {StatusCode::NOT_FOUND,
                absl::StrCat("Snapshot not found: ", request->snapshot_id())};
    }

    {
        absl::MutexLock state_lock(&state_mutex_);
        auto& metadata = user_metadata_[request->snapshot_id()];
        if (request->has_display_name()) {
            metadata.display_name = request->display_name();
        }
        if (request->has_description()) {
            metadata.description = request->description();
        }
    }

    PopulateSnapshotProto(*found, response);
    return Status::OK;
}

Status HypervisorServiceImpl::CreateSnapshot(ServerContext* /*context*/,
                                             const CreateSnapshotRequest* request,
                                             Operation* response) {
    if (ContainsInvalidPathChars(request->snapshot_id())) {
        return {StatusCode::INVALID_ARGUMENT,
                "snapshot_id must not contain path separators or '..'."};
    }

    const std::string snapshot_id =
            request->snapshot_id().empty() ? GenerateSnapshotId() : request->snapshot_id();

    const SnapshotOperationMetadata initial_meta =
            MakeOperationMetadata(snapshot_id, SnapshotOperationMetadata::PREPARING, 0);
    const Operation started_op =
            operations_service_->StartOperation("snapshot/create", initial_meta);

    const absl::Status save_status =
            vm_operations_.SaveSnapshot(snapshot_id.c_str(), /*overwrite=*/true);
    if (!save_status.ok()) {
        // The operation is registered, so report the failure through the Operation
        // (AIP-151). A non-OK gRPC status would cause the response to be discarded.
        *response =
                operations_service_->FailOperation(started_op.name(), save_status, &initial_meta);
        return Status::OK;
    }

    {
        absl::MutexLock state_lock(&state_mutex_);
        user_metadata_[snapshot_id] = UserSnapshotMetadata{
            .display_name = request->display_name(),
            .description = request->description(),
        };
    }

    VmOperations::SnapshotEntry entry{
        .id = snapshot_id,
        .name = request->display_name().empty() ? snapshot_id : request->display_name(),
        .timestamp = absl::Now(),
    };
    vm_operations_
            .ListSnapshots([&](VmOperations::SnapshotEntry listed) {
                if (listed.id == snapshot_id) {
                    entry = std::move(listed);
                }
            })
            .IgnoreError();

    Snapshot snapshot_proto;
    PopulateSnapshotProto(entry, &snapshot_proto);

    const SnapshotOperationMetadata final_meta =
            MakeOperationMetadata(snapshot_id, SnapshotOperationMetadata::TRANSFERRING, 100);
    *response =
            operations_service_->CompleteOperation(started_op.name(), snapshot_proto, &final_meta);
    return Status::OK;
}

Status HypervisorServiceImpl::RestoreSnapshot(ServerContext* /*context*/,
                                              const RestoreSnapshotRequest* request,
                                              Operation* response) {
    if (request->snapshot_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "snapshot_id cannot be empty."};
    }

    std::optional<VmOperations::SnapshotEntry> target_entry;
    const absl::Status list_status =
            vm_operations_.ListSnapshots([&](VmOperations::SnapshotEntry entry) {
                if (entry.id == request->snapshot_id()) {
                    target_entry = std::move(entry);
                }
            });
    if (!list_status.ok()) {
        return AbslStatusToGrpcStatus(list_status);
    }
    if (!target_entry.has_value()) {
        return {StatusCode::NOT_FOUND,
                absl::StrCat("Snapshot not found: ", request->snapshot_id())};
    }

    const SnapshotOperationMetadata initial_meta =
            MakeOperationMetadata(request->snapshot_id(), SnapshotOperationMetadata::PREPARING, 0);
    const Operation started_op =
            operations_service_->StartOperation("snapshot/restore", initial_meta);

    const absl::Status load_status =
            vm_operations_.LoadSnapshot(request->snapshot_id().c_str(), /*andResume=*/true);
    if (!load_status.ok()) {
        *response =
                operations_service_->FailOperation(started_op.name(), load_status, &initial_meta);
        return Status::OK;
    }

    Snapshot snapshot_proto;
    PopulateSnapshotProto(*target_entry, &snapshot_proto);

    const SnapshotOperationMetadata final_meta = MakeOperationMetadata(
            request->snapshot_id(), SnapshotOperationMetadata::TRANSFERRING, 100);
    *response =
            operations_service_->CompleteOperation(started_op.name(), snapshot_proto, &final_meta);
    return Status::OK;
}

Status HypervisorServiceImpl::DeleteSnapshot(ServerContext* /*context*/,
                                             const DeleteSnapshotRequest* request,
                                             DeleteSnapshotResponse* /*response*/) {
    if (request->snapshot_id().empty() || ContainsInvalidPathChars(request->snapshot_id())) {
        return {StatusCode::INVALID_ARGUMENT,
                "snapshot_id must not be empty or contain path separators or '..'."};
    }

    const absl::Status status = vm_operations_.DeleteSnapshot(request->snapshot_id().c_str());
    if (!status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }

    absl::MutexLock state_lock(&state_mutex_);
    user_metadata_.erase(request->snapshot_id());
    if (quickboot_target_id_ == request->snapshot_id()) {
        quickboot_target_id_.clear();
    }
    return Status::OK;
}

Status HypervisorServiceImpl::SetQuickbootTarget(ServerContext* /*context*/,
                                                 const SetQuickbootTargetRequest* request,
                                                 SetQuickbootTargetResponse* /*response*/) {
    if (!request->has_snapshot_id() || request->snapshot_id().empty()) {
        absl::MutexLock state_lock(&state_mutex_);
        quickboot_target_id_.clear();
        return Status::OK;
    }

    bool exists = false;
    const absl::Status status =
            vm_operations_.ListSnapshots([&](VmOperations::SnapshotEntry entry) {
                if (entry.id == request->snapshot_id()) {
                    exists = true;
                }
            });
    if (!status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    if (!exists) {
        return {StatusCode::NOT_FOUND,
                absl::StrCat("Snapshot not found: ", request->snapshot_id())};
    }

    absl::MutexLock state_lock(&state_mutex_);
    quickboot_target_id_ = request->snapshot_id();
    return Status::OK;
}

Status HypervisorServiceImpl::ExportSnapshot(ServerContext* /*context*/,
                                             const ExportSnapshotRequest* request,
                                             Operation* /*response*/) {
    if (request->snapshot_id().empty() ||
        request->destination_case() == ExportSnapshotRequest::DESTINATION_NOT_SET) {
        return {StatusCode::INVALID_ARGUMENT,
                "Both snapshot_id and destination (storage_uri or presigned_upload_url) are "
                "required."};
    }
    return {StatusCode::FAILED_PRECONDITION,
            "Cloud snapshot export is not supported on this emulator instance."};
}

Status HypervisorServiceImpl::ImportSnapshot(ServerContext* /*context*/,
                                             const ImportSnapshotRequest* request,
                                             Operation* /*response*/) {
    if (ContainsInvalidPathChars(request->snapshot_id()) ||
        request->source_case() == ImportSnapshotRequest::SOURCE_NOT_SET) {
        return {StatusCode::INVALID_ARGUMENT,
                "source (storage_uri or presigned_download_url) is required and snapshot_id must "
                "not contain path separators or '..'."};
    }
    return {StatusCode::FAILED_PRECONDITION,
            "Cloud snapshot import is not supported on this emulator instance."};
}

}  // namespace goldfish::grpc::v2
