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
using ::android::emulation::control::UniqueEventStreamWriter;
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
                                                int32_t progress_percent,
                                                int64_t transferred_bytes = 0,
                                                int64_t total_bytes = 0) {
    SnapshotOperationMetadata metadata;
    metadata.set_snapshot_id(std::string(snapshot_id));
    metadata.set_phase(phase);
    metadata.set_progress_percent(progress_percent);
    metadata.set_transferred_bytes(transferred_bytes);
    metadata.set_total_bytes(total_bytes);
    return metadata;
}

SnapshotOperationMetadata MakeOperationMetadataFromProgress(
        std::string_view snapshot_id, const VmOperations::SnapshotProgress& progress, bool done) {
    const auto phase = (progress.phase == VmOperations::SnapshotPhase::Preparing && !done)
                               ? SnapshotOperationMetadata::PREPARING
                               : SnapshotOperationMetadata::TRANSFERRING;
    const int32_t percent = done ? 100 : progress.progress_percent();
    const int64_t total = progress.total_bytes;
    const int64_t transferred = (done && total > 0) ? total : progress.transferred_bytes;
    SnapshotOperationMetadata metadata =
            MakeOperationMetadata(snapshot_id, phase, /*progress_percent=*/percent,
                                  /*transferred_bytes=*/transferred, /*total_bytes=*/total);
    if (!done && !progress.active_component.empty()) {
        metadata.set_active_component(progress.active_component);
    }
    metadata.set_completed_devices((done && progress.total_devices > 0)
                                           ? progress.total_devices
                                           : progress.completed_devices);
    metadata.set_total_devices(progress.total_devices);
    return metadata;
}

}  // namespace

HypervisorServiceImpl::HypervisorServiceImpl(
        VmOperations& vm_operations, std::shared_ptr<OperationsServiceImpl> operations_service)
        : vm_operations_(vm_operations)
        , operations_service_(std::move(operations_service))
        , run_state_callback_id_(vm_operations_.RunStateEvents().AddCallback(
                  [this](EmuRunState state) { OnRunStateChanged(state); }))
        , snapshot_callback_id_(vm_operations_.SnapshotEvents().AddCallback(
                  [this](const VmOperations::SnapshotProgress& progress) {
                      OnSnapshotStateChanged(progress);
                  })) {
    DCHECK(operations_service_ != nullptr) << "OperationsServiceImpl cannot be null.";
    operations_service_->SetMetadataRefresher(
            [this](Operation* op) { RefreshOperationMetadata(op); });
}

HypervisorServiceImpl::~HypervisorServiceImpl() {
    vm_operations_.SnapshotEvents().RemoveCallback(snapshot_callback_id_);
    vm_operations_.RunStateEvents().RemoveCallback(run_state_callback_id_);
    if (operations_service_ != nullptr) {
        operations_service_->SetMetadataRefresher(nullptr);
    }
}

void HypervisorServiceImpl::OnRunStateChanged(EmuRunState state) {
    if (state == EmuRunState::Shutdown) {
        shutting_down_.store(true, std::memory_order_release);
    }
    if (state == EmuRunState::SaveVm || state == EmuRunState::RestoreVm) {
        return;
    }
    VmState vm_state;
    BuildVmState(state, /*snap_progress=*/VmOperations::SnapshotProgress{}, &vm_state);
    vm_state_events_.FireEvent(vm_state);
}

void HypervisorServiceImpl::RefreshOperationMetadata(Operation* op) const {
    if (op == nullptr || op->done()) {
        return;
    }
    SnapshotOperationMetadata meta;
    if (!op->metadata().UnpackTo(&meta)) {
        return;
    }
    const auto progress = vm_operations_.GetSnapshotProgress();
    if (progress.operation == VmOperations::SnapshotOperationType::Idle ||
        progress.phase == VmOperations::SnapshotPhase::Completed ||
        progress.phase == VmOperations::SnapshotPhase::Failed) {
        return;
    }
    if (!progress.snapshot_id.empty() && !meta.snapshot_id().empty() &&
        progress.snapshot_id != meta.snapshot_id()) {
        return;
    }
    const std::string snapshot_id =
            meta.snapshot_id().empty() ? progress.snapshot_id : meta.snapshot_id();
    const SnapshotOperationMetadata updated =
            MakeOperationMetadataFromProgress(snapshot_id, progress, /*done=*/false);
    (void)op->mutable_metadata()->PackFrom(updated);
}

void HypervisorServiceImpl::OnSnapshotStateChanged(const VmOperations::SnapshotProgress& progress) {
    if (progress.operation == VmOperations::SnapshotOperationType::Idle ||
        progress.phase == VmOperations::SnapshotPhase::None) {
        return;
    }

    VmState vm_state;
    BuildVmState(vm_operations_.GetRunState(), progress, &vm_state);
    vm_state_events_.FireEvent(vm_state);

    const std::string snapshot_id =
            progress.snapshot_id.empty() ? "default_boot" : progress.snapshot_id;
    const SnapshotOperationKey op_key{progress.operation, snapshot_id};

    if (progress.phase == VmOperations::SnapshotPhase::Preparing ||
        progress.phase == VmOperations::SnapshotPhase::Transferring) {
        std::string existing_lro_name;
        {
            absl::MutexLock state_lock(&state_mutex_);
            if (auto it = snapshot_operations_.find(op_key); it != snapshot_operations_.end()) {
                existing_lro_name = it->second.lro_name;
            }
        }
        const SnapshotOperationMetadata meta =
                MakeOperationMetadataFromProgress(snapshot_id, progress, /*done=*/false);
        if (!existing_lro_name.empty()) {
            operations_service_->UpdateOperationMetadata(existing_lro_name, meta);
            return;
        }

        const std::string_view prefix =
                (progress.operation == VmOperations::SnapshotOperationType::Save)
                        ? "snapshot/create"
                        : "snapshot/restore";
        const Operation started = operations_service_->StartOperation(prefix, meta);

        absl::MutexLock state_lock(&state_mutex_);
        snapshot_operations_[op_key] = TrackedSnapshotOperation{
            .lro_name = started.name(),
            .snapshot_id = snapshot_id,
            .is_rpc = false,
        };
        return;
    }

    TrackedSnapshotOperation tracked;
    {
        absl::MutexLock state_lock(&state_mutex_);
        auto it = snapshot_operations_.find(op_key);
        if (it == snapshot_operations_.end()) {
            return;
        }
        tracked = std::move(it->second);
        snapshot_operations_.erase(it);
    }

    if (tracked.is_rpc) {
        return;
    }

    const std::string& active_id = tracked.snapshot_id.empty() ? snapshot_id : tracked.snapshot_id;
    if (progress.phase == VmOperations::SnapshotPhase::Failed) {
        const SnapshotOperationMetadata fail_meta =
                MakeOperationMetadataFromProgress(active_id, progress, /*done=*/false);
        const absl::Status err =
                absl::InternalError(progress.error_message.empty() ? "Snapshot operation failed"
                                                                   : progress.error_message);
        (void)operations_service_->FailOperation(tracked.lro_name, err, &fail_meta);
        return;
    }

    Snapshot snapshot_proto;
    PopulateSnapshotProto(
            VmOperations::SnapshotEntry{
                .name = active_id,
                .timestamp = absl::Now(),
            },
            &snapshot_proto);
    if (progress.disk_bytes > 0) {
        snapshot_proto.set_size_bytes(progress.disk_bytes);
    }
    const SnapshotOperationMetadata final_meta =
            MakeOperationMetadataFromProgress(active_id, progress, /*done=*/true);
    (void)operations_service_->CompleteOperation(tracked.lro_name, snapshot_proto, &final_meta);
}

void HypervisorServiceImpl::PopulateVmState(VmState* state) const {
    BuildVmState(vm_operations_.GetRunState(), vm_operations_.GetSnapshotProgress(), state);
}

void HypervisorServiceImpl::BuildVmState(EmuRunState run_state,
                                         const VmOperations::SnapshotProgress& snap_progress,
                                         VmState* state) const {
    if (run_state == EmuRunState::Shutdown) {
        shutting_down_.store(true, std::memory_order_release);
    }
    const bool is_shutting_down =
            shutting_down_.load(std::memory_order_acquire) || (run_state == EmuRunState::Shutdown);
    const bool snapshot_in_progress =
            snap_progress.phase == VmOperations::SnapshotPhase::Preparing ||
            snap_progress.phase == VmOperations::SnapshotPhase::Transferring;

    if ((snap_progress.phase == VmOperations::SnapshotPhase::Completed ||
         snap_progress.phase == VmOperations::SnapshotPhase::Failed) &&
        (run_state == EmuRunState::SaveVm || run_state == EmuRunState::RestoreVm)) {
        run_state = EmuRunState::Running;
    }

    if (snapshot_in_progress &&
        (snap_progress.operation == VmOperations::SnapshotOperationType::Save ||
         snap_progress.operation == VmOperations::SnapshotOperationType::Load)) {
        const bool is_save = snap_progress.operation == VmOperations::SnapshotOperationType::Save;
        state->set_state(is_save ? (is_shutting_down ? VmState::SHUTTING_DOWN
                                                     : VmState::SAVING_SNAPSHOT)
                                 : VmState::RESTORING_SNAPSHOT);
        const std::string snapshot_id =
                snap_progress.snapshot_id.empty() ? "default_boot" : snap_progress.snapshot_id;
        *state->mutable_snapshot_progress() =
                MakeOperationMetadataFromProgress(snapshot_id, snap_progress, /*done=*/false);
    } else {
        state->clear_snapshot_progress();
        if (is_shutting_down) {
            state->set_state(VmState::SHUTTING_DOWN);
        } else {
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
                state->set_state(vm_operations_.IsRunning() ? VmState::RUNNING : VmState::STOPPED);
                break;
            }
        }
    }

    const auto config = vm_operations_.GetConfiguration();
    state->set_allocated_cpu_core_count(config.number_of_cpu_cores > 0 ? config.number_of_cpu_cores
                                                                       : 1);
}

Status HypervisorServiceImpl::GetVmState(ServerContext* /*context*/,
                                         const GetVmStateRequest* /*request*/, VmState* response) {
    PopulateVmState(response);
    return Status::OK;
}

ServerWriteReactor<VmState>* HypervisorServiceImpl::StreamVmState(
        CallbackServerContext* /*context*/, const StreamVmStateRequest* /*request*/) {
    // The writer keeps a raw pointer to vm_state_events_. That is safe because the
    // main-loop exit hook destroys the gRPC server, and with it every writer, before
    // qemu_cleanup() destroys the service and the AVD universe that owns VmOperations.
    return new UniqueEventStreamWriter<VmState>(&vm_state_events_,
                                                [this](VmState* state) { PopulateVmState(state); });
}

Status HypervisorServiceImpl::PauseVm(ServerContext* /*context*/, const PauseVmRequest* /*request*/,
                                      VmState* response) {
    if (const absl::Status status = vm_operations_.Pause(); !status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::ResumeVm(ServerContext* /*context*/,
                                       const ResumeVmRequest* /*request*/, VmState* response) {
    if (const absl::Status status = vm_operations_.Resume(); !status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::ResetVmInternal(VmState* response) {
    if (const absl::Status status = vm_operations_.Reset(); !status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    shutting_down_.store(false, std::memory_order_release);
    PopulateVmState(response);
    return Status::OK;
}

Status HypervisorServiceImpl::ResetVm(ServerContext* /*context*/, const ResetVmRequest* /*request*/,
                                      VmState* response) {
    return ResetVmInternal(response);
}

Status HypervisorServiceImpl::HardResetVm(ServerContext* /*context*/,
                                          const HardResetVmRequest* /*request*/,
                                          VmState* response) {
    return ResetVmInternal(response);
}

Status HypervisorServiceImpl::StopVm(ServerContext* /*context*/, const StopVmRequest* request,
                                     VmState* response) {
    if (request != nullptr && request->save_quickboot_snapshot()) {
        LOG(WARNING) << "save_quickboot_snapshot is currently unimplemented; relying on "
                        "QEMU default shutdown lifecycle.";
    }
    if (const absl::Status status = vm_operations_.Shutdown(); !status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    shutting_down_.store(true, std::memory_order_release);
    PopulateVmState(response);
    vm_state_events_.FireEvent(*response);
    return Status::OK;
}

void HypervisorServiceImpl::PopulateSnapshotProto(const VmOperations::SnapshotEntry& entry,
                                                  Snapshot* out) const {
    out->set_snapshot_id(entry.name);

    std::string display_name = entry.name;
    std::string description;
    bool is_quickboot = false;
    {
        absl::MutexLock state_lock(&state_mutex_);
        is_quickboot = (entry.name == quickboot_target_id_);
        if (auto it = user_metadata_.find(entry.name); it != user_metadata_.end()) {
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

Status HypervisorServiceImpl::FindSnapshotEntry(
        std::string_view snapshot_id, VmOperations::SnapshotEntry* out_entry) const {
    if (snapshot_id.empty()) {
        return {StatusCode::INVALID_ARGUMENT, "snapshot_id cannot be empty."};
    }

    std::optional<VmOperations::SnapshotEntry> found;
    const absl::Status status =
            vm_operations_.ListSnapshots([&](VmOperations::SnapshotEntry entry) {
                if (entry.name == snapshot_id) {
                    found = std::move(entry);
                }
            });
    if (!status.ok()) {
        return AbslStatusToGrpcStatus(status);
    }
    if (!found.has_value()) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Snapshot not found: ", snapshot_id)};
    }
    if (out_entry != nullptr) {
        *out_entry = std::move(*found);
    }
    return Status::OK;
}

Status HypervisorServiceImpl::GetSnapshot(ServerContext* /*context*/,
                                          const GetSnapshotRequest* request, Snapshot* response) {
    VmOperations::SnapshotEntry found;
    if (const Status status = FindSnapshotEntry(request->snapshot_id(), &found); !status.ok()) {
        return status;
    }

    PopulateSnapshotProto(found, response);
    return Status::OK;
}

Status HypervisorServiceImpl::UpdateSnapshot(ServerContext* /*context*/,
                                             const UpdateSnapshotRequest* request,
                                             Snapshot* response) {
    VmOperations::SnapshotEntry found;
    if (const Status status = FindSnapshotEntry(request->snapshot_id(), &found); !status.ok()) {
        return status;
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

    PopulateSnapshotProto(found, response);
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
    const SnapshotOperationKey op_key{VmOperations::SnapshotOperationType::Save, snapshot_id};
    {
        absl::MutexLock state_lock(&state_mutex_);
        snapshot_operations_[op_key] = TrackedSnapshotOperation{
            .lro_name = started_op.name(),
            .snapshot_id = snapshot_id,
            .is_rpc = true,
        };
    }

    const absl::Status save_status = vm_operations_.SaveSnapshot(snapshot_id, /*overwrite=*/true);
    const auto progress = vm_operations_.GetSnapshotProgress();
    const auto expected_phase = save_status.ok() ? VmOperations::SnapshotPhase::Completed
                                                 : VmOperations::SnapshotPhase::Failed;
    const bool matches_progress = progress.operation == VmOperations::SnapshotOperationType::Save &&
                                  progress.phase == expected_phase &&
                                  progress.snapshot_id == snapshot_id;
    if (!matches_progress) {
        absl::MutexLock state_lock(&state_mutex_);
        snapshot_operations_.erase(op_key);
    }
    if (!save_status.ok()) {
        const SnapshotOperationMetadata fail_meta =
                matches_progress
                        ? MakeOperationMetadataFromProgress(snapshot_id, progress, /*done=*/false)
                        : initial_meta;
        // The operation is registered, so report the failure through the Operation
        // (AIP-151). A non-OK gRPC status would cause the response to be discarded.
        *response = operations_service_->FailOperation(started_op.name(), save_status, &fail_meta);
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
        .name = snapshot_id,
        .timestamp = absl::Now(),
    };
    vm_operations_
            .ListSnapshots([&](VmOperations::SnapshotEntry listed) {
                if (listed.name == snapshot_id) {
                    entry = std::move(listed);
                }
            })
            .IgnoreError();

    Snapshot snapshot_proto;
    PopulateSnapshotProto(entry, &snapshot_proto);
    if (matches_progress && progress.disk_bytes > 0) {
        snapshot_proto.set_size_bytes(progress.disk_bytes);
    }

    const SnapshotOperationMetadata final_meta = MakeOperationMetadataFromProgress(
            snapshot_id, matches_progress ? progress : VmOperations::SnapshotProgress{},
            /*done=*/true);
    *response =
            operations_service_->CompleteOperation(started_op.name(), snapshot_proto, &final_meta);
    return Status::OK;
}

Status HypervisorServiceImpl::RestoreSnapshot(ServerContext* /*context*/,
                                              const RestoreSnapshotRequest* request,
                                              Operation* response) {
    VmOperations::SnapshotEntry target_entry;
    if (const Status status = FindSnapshotEntry(request->snapshot_id(), &target_entry);
        !status.ok()) {
        return status;
    }

    const SnapshotOperationMetadata initial_meta =
            MakeOperationMetadata(request->snapshot_id(), SnapshotOperationMetadata::PREPARING, 0);
    const Operation started_op =
            operations_service_->StartOperation("snapshot/restore", initial_meta);
    const SnapshotOperationKey op_key{VmOperations::SnapshotOperationType::Load,
                                      request->snapshot_id()};
    {
        absl::MutexLock state_lock(&state_mutex_);
        snapshot_operations_[op_key] = TrackedSnapshotOperation{
            .lro_name = started_op.name(),
            .snapshot_id = request->snapshot_id(),
            .is_rpc = true,
        };
    }

    const absl::Status load_status =
            vm_operations_.LoadSnapshot(request->snapshot_id(), /*and_resume=*/true);
    const auto progress = vm_operations_.GetSnapshotProgress();
    const auto expected_phase = load_status.ok() ? VmOperations::SnapshotPhase::Completed
                                                 : VmOperations::SnapshotPhase::Failed;
    const bool matches_progress = progress.operation == VmOperations::SnapshotOperationType::Load &&
                                  progress.phase == expected_phase &&
                                  progress.snapshot_id == request->snapshot_id();
    if (!matches_progress) {
        absl::MutexLock state_lock(&state_mutex_);
        snapshot_operations_.erase(op_key);
    }
    if (!load_status.ok()) {
        const SnapshotOperationMetadata fail_meta =
                matches_progress
                        ? MakeOperationMetadataFromProgress(request->snapshot_id(), progress,
                                                            /*done=*/false)
                        : initial_meta;
        *response = operations_service_->FailOperation(started_op.name(), load_status, &fail_meta);
        return Status::OK;
    }

    Snapshot snapshot_proto;
    PopulateSnapshotProto(target_entry, &snapshot_proto);

    const SnapshotOperationMetadata final_meta = MakeOperationMetadataFromProgress(
            request->snapshot_id(), matches_progress ? progress : VmOperations::SnapshotProgress{},
            /*done=*/true);
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

    const absl::Status status = vm_operations_.DeleteSnapshot(request->snapshot_id());
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

    if (const Status status = FindSnapshotEntry(request->snapshot_id(), nullptr); !status.ok()) {
        return status;
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
