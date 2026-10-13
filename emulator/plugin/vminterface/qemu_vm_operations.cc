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
#include "android/goldfish/qemu_vm_operations.h"

#include <array>
#include <memory>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/goldfish/vm_interface.h"
#include "android/status/status_macros.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/metrics/snapshot_metrics.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/runstate.h"
#include "migration/snapshot.h"
#include "block/snapshot.h"
}
// IWYU pragma: end_keep
// clang-format on

// Remove leaked shutdown redefinition from external/qemu/include/sysemu/os-win32.h
#undef shutdown

#include "qemu_machine_info.h"
#include "vm_lock.h"

namespace android {
namespace goldfish {
namespace {
absl::Status ToStatus(const absl::StatusCode code, std::string_view prefix, Error** errp,
                      std::string_view remediation) {
    const char* detail =
            (errp && *errp) ? ::error_get_pretty(*errp) : "QEMU reported an unspecified error";
    absl::Status status(code, absl::StrCat(prefix, ": ", detail, ". ", remediation));
    if (errp && *errp) {
        ::error_free(*errp);
        *errp = nullptr;
    }
    return status;
}
absl::Status MakeInvalidTransitionError(std::string_view action, EmuRunState current_state,
                                        std::string_view expected_states) {
    switch (current_state) {
    case EmuRunState::Shutdown:
        return absl::FailedPreconditionError(absl::StrCat(
                "Cannot ", action, " VM: emulator is shutting down (current state: ", current_state,
                ", expected: ", expected_states,
                "). Launch a new emulator instance to start a new session."));
    case EmuRunState::SaveVm:
    case EmuRunState::RestoreVm:
    case EmuRunState::InMigrate:
    case EmuRunState::FinishMigrate:
    case EmuRunState::Colo:
        return absl::FailedPreconditionError(absl::StrCat(
                "Cannot ", action, " VM: snapshot or migration is in progress (current state: ",
                current_state, ", expected: ", expected_states,
                "). Wait for the operation to complete and retry."));
    default:
        return absl::FailedPreconditionError(absl::StrCat(
                "Cannot ", action, " VM from current state ", current_state, " (expected ",
                expected_states, "). Reset the VM or shut down the emulator to recover."));
    }
}
}  // namespace

static_assert(static_cast<int>(QemuShutdownCause::Max) == static_cast<int>(SHUTDOWN_CAUSE__MAX));
static_assert(static_cast<int>(EmuRunState::Max) == static_cast<int>(RUN_STATE__MAX));

/**
 * @brief QemuVmOperations class implementing the VmOperations interface.
 *
 * This class provides a concrete implementation of the VmOperations interface
 * for interacting with a QEMU virtual machine. Mutating and snapshot methods
 * execute synchronously on `qemu_loop` via `PostAndWait` and must not be called
 * from either `event_loop` or `qemu_loop`.
 */
class QemuVmOperations : public VmOperations {
  public:
    // RecursiveScopedVmLock allows registration/deregistration to run safely on
    // both the QEMU main thread as well as external threads during startup/teardown.
    QemuVmOperations(::goldfish::async::EventLoop* event_loop,
                     ::goldfish::async::EventLoop* qemu_loop)
            : VmOperations(event_loop), event_loop_(event_loop), qemu_loop_(qemu_loop) {
        DCHECK(event_loop_ != nullptr);
        DCHECK(qemu_loop_ != nullptr);
        DCHECK(event_loop_ != qemu_loop_)
                << "event_loop and qemu_loop must be distinct event loops.";
        RecursiveScopedVmLock lock;
        change_state_entry_ = qemu_add_vm_change_state_handler(OnVmChangeState, this);
    }
    ~QemuVmOperations() override {
        if (change_state_entry_) {
            RecursiveScopedVmLock lock;
            qemu_del_vm_change_state_handler(change_state_entry_);
        }
    }

    absl::Status Stop() override { return Pause(); }

    absl::Status Start() override { return Resume(); }

    absl::Status Reset() override {
        return RunOnQemuLoop([this] {
            RETURN_IF_ERROR(CheckCanExecuteAction("reset"));
            qemu_system_reset_request(SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
            return absl::OkStatus();
        });
    }

    absl::Status Shutdown() override {
        return RunOnQemuLoop([this] {
            if (GetRunState() == EmuRunState::Shutdown) {
                return absl::OkStatus();
            }
            RETURN_IF_ERROR(CheckCanExecuteAction("shut down"));
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
            return absl::OkStatus();
        });
    }

    absl::Status Pause() override {
        return RunOnQemuLoop([this] {
            switch (const EmuRunState state = GetRunState()) {
            case EmuRunState::Paused:
                return absl::OkStatus();
            case EmuRunState::Running:
            case EmuRunState::Suspended: {
                const int ret = vm_stop(RUN_STATE_PAUSED);
                if (ret < 0) {
                    return absl::ErrnoToStatus(
                            -ret,
                            absl::StrCat("Cannot pause VM from current state ", state,
                                         ": QEMU vm_stop(RUN_STATE_PAUSED) failed (code ", ret,
                                         "). Reset the VM or check host storage availability"));
                }
                if (ret > 0) {
                    return absl::InternalError(absl::StrCat(
                            "Cannot pause VM from current state ", state,
                            ": QEMU vm_stop(RUN_STATE_PAUSED) device stop callback failed with "
                            "code ",
                            ret, ". Reset the VM to recover."));
                }
                return absl::OkStatus();
            }
            default:
                return MakeInvalidTransitionError(
                        /*action=*/"pause", /*current_state=*/state,
                        /*expected_states=*/"Running, Paused, or Suspended");
            }
        });
    }

    absl::Status Resume() override {
        return RunOnQemuLoop([this] {
            switch (const EmuRunState state = GetRunState()) {
            case EmuRunState::Running:
                return absl::OkStatus();
            case EmuRunState::Paused:
            case EmuRunState::Suspended:
            case EmuRunState::PreLaunch:
            case EmuRunState::Debug:
            case EmuRunState::PostMigrate:
                vm_start();
                return absl::OkStatus();
            default:
                return MakeInvalidTransitionError(
                        /*action=*/"resume", /*current_state=*/state,
                        /*expected_states=*/
                        "Running, Paused, Suspended, Debug, PreLaunch, or PostMigrate");
            }
        });
    }

    /**
     * @brief Checks if the QEMU virtual machine is currently running.
     *
     * @return True if the VM is running, false otherwise.
     */
    bool IsRunning() override { return runstate_get() == RUN_STATE_RUNNING; }

    /**
     * @brief Gets the configuration of the QEMU virtual machine.
     *
     * @return The VM's configuration.
     * @note This is a placeholder implementation.
     *       A real implementation would need to query QEMU for the actual
     * configuration.
     */
    VmConfiguration GetConfiguration() override {
        VmConfiguration config;
        const std::string_view accel = ::accel_name_cwrap();

        // ** Update this when you add a new hypervisor **
        // Make sure that ac->name matches with pair.first!
        constexpr static std::array<std::pair<std::string_view, VmHypervisorType>, 5>
                kHyperVisorData = {{
                    {"unknown", VmHypervisorType::Unknown},  // Unused.
                    {"tcg", VmHypervisorType::None},         // qemu/accel/tcg/tcg-all.c
                    {"KVM", VmHypervisorType::Kvm},          // qemu/accel/kvm/kvm-all.c
                    {"HVF", VmHypervisorType::Hvf},          // qemu/accel/hvf/hvf-accel-ops.c
                    {"WHPX", VmHypervisorType::Whpx},        // qemu/target/i386/whpx/whpx-all.c
                }};
        static_assert(std::size(kHyperVisorData) == static_cast<size_t>(VmHypervisorType::Max));

        config.hypervisor_type = VmHypervisorType::Unknown;
        for (const auto& pair : kHyperVisorData) {
            if (pair.first == accel) {
                config.hypervisor_type = pair.second;
                break;
            }
        }
        config.number_of_cpu_cores = ::cpu_count_cwrap();
        config.cpu_type = ::cpu_type_cwrap();
        return config;
    }

    /**
     * @brief Gets the current run state of the QEMU emulator.
     *
     * @return The emulator's run state.
     */
    EmuRunState GetRunState() override { return static_cast<EmuRunState>(runstate_get()); }

    /**
     * @brief Requests a system shutdown with a specified reason.
     *
     * @param reason The reason for the shutdown request.
     */
    void SystemShutdownRequest(QemuShutdownCause reason) override {
        qemu_system_shutdown_request(static_cast<ShutdownCause>(reason));
    }

    absl::Status SaveSnapshot(std::string name, const bool overwrite) override {
        return RunOnQemuLoop([this, name = std::move(name), overwrite] {
            absl::Time start = absl::Now();
            RETURN_IF_ERROR(CheckCanExecuteAction("save snapshot for"));
            auto& avd = ::goldfish::avd::GetAvd();
            avd.SetGenericSnapshotInProgress(true);
            auto cleanup = absl::MakeCleanup([&avd] { avd.SetGenericSnapshotInProgress(false); });
            ::Error* errp = nullptr;
            bool ok = ::save_snapshot(name.c_str(), overwrite, /*vmstate=*/nullptr,
                                      /*has_devices=*/false, /*devices=*/nullptr, &errp);
            ::goldfish::metrics::ReportGenericSnapshotSave(avd.GetMetricsReporter(), name, ok,
                                                           absl::Now() - start);
            if (!ok) {
                return ToStatus(
                        absl::StatusCode::kInternal,
                        /*prefix=*/absl::StrCat("Cannot save snapshot '", name, "'"), &errp,
                        /*remediation=*/
                        "Verify that the writable qcow2 disk image has free space and retry.");
            }
            return absl::OkStatus();
        });
    }

    absl::Status LoadSnapshot(std::string id_or_name, const bool and_resume) override {
        return RunOnQemuLoop([this, id_or_name = std::move(id_or_name), and_resume] {
            absl::Time start = absl::Now();
            RETURN_IF_ERROR(CheckCanExecuteAction("load snapshot into"));
            auto& avd = ::goldfish::avd::GetAvd();
            ::Error* errp = nullptr;

            ::vm_stop(RUN_STATE_RESTORE_VM);

            avd.SetGenericSnapshotInProgress(true);
            auto cleanup = absl::MakeCleanup([&avd] { avd.SetGenericSnapshotInProgress(false); });
            bool ok = ::load_snapshot(id_or_name.c_str(), /*vmstate=*/nullptr,
                                      /*has_devices=*/false, /*devices=*/nullptr, &errp);
            ::goldfish::metrics::ReportGenericSnapshotLoad(avd.GetMetricsReporter(), id_or_name, ok,
                                                           absl::Now() - start);
            if (!ok) {
                // do_vm_stop(RUN_STATE_RESTORE_VM) only transitions from live states
                // (Running/Suspended); non-live states (e.g., Paused) remain unchanged.
                if (::runstate_get() == ::RUN_STATE_RESTORE_VM) {
                    ::load_snapshot_resume(::RUN_STATE_PRELAUNCH);
                }
                return ToStatus(absl::StatusCode::kInternal,
                                /*prefix=*/absl::StrCat("Cannot load snapshot '", id_or_name, "'"),
                                &errp,
                                /*remediation=*/
                                "Verify the snapshot exists with ListSnapshots, or reset/resume "
                                "the VM.");
            }

            if (and_resume) {
                ::load_snapshot_resume(::RUN_STATE_RUNNING);
            }

            return absl::OkStatus();
        });
    }

    void LoadSnapshotResume(const EmuRunState ers) override {
        if (absl::Status status =
                    RunOnQemuLoop([ers] { ::load_snapshot_resume(static_cast<::RunState>(ers)); });
            !status.ok()) {
            LOG(WARNING) << "Cannot resume VM to state " << ers
                         << " after loading snapshot: " << status;
        }
    }

    absl::Status DeleteSnapshot(std::string id_or_name) override {
        return RunOnQemuLoop([this, id_or_name = std::move(id_or_name)] {
            RETURN_IF_ERROR(CheckCanExecuteAction("delete snapshot from"));
            ::Error* errp = nullptr;
            if (!::delete_snapshot(id_or_name.c_str(), /*has_devices=*/false, /*devices=*/nullptr,
                                   &errp)) {
                return ToStatus(
                        absl::StatusCode::kInternal,
                        /*prefix=*/absl::StrCat("Cannot delete snapshot '", id_or_name, "'"), &errp,
                        /*remediation=*/
                        "Verify that the snapshot exists and the disk image is writable.");
            }

            return absl::OkStatus();
        });
    }

    absl::Status ListSnapshots(const SnapshotEntrySink sink) override {
        return RunOnQemuLoop([sink] {
            ::Error* errp = nullptr;
            ::BlockDriverState* bs = ::bdrv_all_find_vmstate_bs(
                    /*vmstate_bs=*/nullptr, /*has_devices=*/false, /*devices=*/nullptr, &errp);
            if (!bs) {
                return ToStatus(
                        absl::StatusCode::kFailedPrecondition,
                        /*prefix=*/
                        "Cannot list snapshots: no block device supports VM state snapshots", &errp,
                        /*remediation=*/"Ensure the AVD is configured with a qcow2 disk image.");
            }

            ::QEMUSnapshotInfo* qsi = nullptr;
            const int qsi_size = ::bdrv_snapshot_list(bs, &qsi);
            if (qsi_size < 0) {
                return absl::ErrnoToStatus(
                        -qsi_size,
                        "Cannot list snapshots: failed to read snapshot table from block device. "
                        "Verify that the disk image is accessible and not corrupted.");
            }

            for (int i = 0; i < qsi_size; ++i) {
                SnapshotEntry se = {
                    .id = qsi[i].id_str,
                    .name = qsi[i].name,
                    .timestamp = absl::FromUnixSeconds(qsi[i].date_sec) +
                                 absl::Nanoseconds(qsi[i].date_nsec),
                };

                sink(std::move(se));
            }

            ::g_free(qsi);
            return absl::OkStatus();
        });
    }

  private:
    absl::Status CheckCanExecuteAction(std::string_view action) {
        switch (const EmuRunState state = GetRunState()) {
        case EmuRunState::Shutdown:
        case EmuRunState::SaveVm:
        case EmuRunState::RestoreVm:
        case EmuRunState::InMigrate:
        case EmuRunState::FinishMigrate:
        case EmuRunState::Colo:
            return MakeInvalidTransitionError(
                    action, /*current_state=*/state,
                    /*expected_states=*/"Running, Paused, Suspended, Debug, or fault state");
        default:
            return absl::OkStatus();
        }
    }

    template <typename F>
    absl::Status RunOnQemuLoop(F&& fn) {
        if (event_loop_->IsOnLoopThread()) {
            LOG(FATAL) << "VmOperations cannot be called from the event loop.";
        }
        return qemu_loop_->PostAndWait(std::forward<F>(fn));
    }

    static void OnVmChangeState(void* opaque, bool /*running*/, RunState state) {
        auto* self = static_cast<QemuVmOperations*>(opaque);
        // On the loop thread the dispatcher would run the subscribers inline, under the BQL.
        DCHECK(!self->event_loop_->IsOnLoopThread())
                << "Run state changed on the event loop thread";
        self->RunStateEvents().FireEvent(static_cast<EmuRunState>(state));
    }

    ::goldfish::async::EventLoop* const event_loop_;
    ::goldfish::async::EventLoop* const qemu_loop_;
    VMChangeStateEntry* change_state_entry_{nullptr};
};

std::unique_ptr<VmOperations> CreateQemuVmOperations(::goldfish::async::EventLoop* event_loop,
                                                     ::goldfish::async::EventLoop* qemu_loop) {
    return std::make_unique<QemuVmOperations>(event_loop, qemu_loop);
}

}  // namespace goldfish
}  // namespace android
