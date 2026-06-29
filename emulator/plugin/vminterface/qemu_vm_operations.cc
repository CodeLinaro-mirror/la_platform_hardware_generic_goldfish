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
#include <array>

#include "absl/strings/str_cat.h"

#include "android/goldfish/vm_interface.h"

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
absl::Status ToStatus(const absl::StatusCode code, Error** errp) {
    absl::Status status = absl::Status(code, ::error_get_pretty(*errp));
    ::error_free(*errp);
    *errp = nullptr;
    return status;
}
}  // namespace

static_assert((int)QemuShutdownCause::Max == (int)SHUTDOWN_CAUSE__MAX);

/**
 * @brief QemuVmOperations class implementing the VmOperations interface.
 *
 * This class provides a concrete implementation of the VmOperations interface
 * for interacting with a QEMU virtual machine.
 */
class QemuVmOperations : public VmOperations {
  public:
    QemuVmOperations() = default;
    ~QemuVmOperations() override = default;

    /**
     * @brief Stops the QEMU virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    bool stop() override {
        ScopedVmLock lock;
        return vm_stop(RUN_STATE_PAUSED) == 0;
    }

    /**
     * @brief Starts the QEMU virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    bool start() override {
        ScopedVmLock lock;
        vm_start();
        return true;
    }

    /**
     * @brief Resets the QEMU virtual machine.
     */
    void reset() override {
        ScopedVmLock lock;
        qemu_system_reset_request(SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
    }

    /**
     * @brief Shuts down the QEMU virtual machine.
     */
    void Shutdown() override {
        ScopedVmLock lock;
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
    }

    /**
     * @brief Pauses the QEMU virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    bool pause() override {
        ScopedVmLock lock;
        return vm_stop(RUN_STATE_PAUSED) == 0;
    }

    /**
     * @brief Resumes the QEMU virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    bool resume() override {
        ScopedVmLock lock;
        // Should this be qmp_cont?
        vm_start();
        return true;
    }

    /**
     * @brief Checks if the QEMU virtual machine is currently running.
     *
     * @return True if the VM is running, false otherwise.
     */
    bool isRunning() override { return runstate_get() == RUN_STATE_RUNNING; }

    /**
     * @brief Gets the configuration of the QEMU virtual machine.
     *
     * @return The VM's configuration.
     * @note This is a placeholder implementation.
     *       A real implementation would need to query QEMU for the actual
     * configuration.
     */
    VmConfiguration getConfiguration() override {
        VmConfiguration config;
        const std::string_view accel = ::accel_name_cwrap();

        // ** Update this when you add a new hypervisor **
        // Make sure that ac->name matches with pair.first!
        constexpr static std::array<std::pair<std::string_view, VmHypervisorType>, 5>
                hyperVisorData = {{
                    {"unknown", VmHypervisorType::Unknown},  // Unused.
                    {"tcg", VmHypervisorType::None},         // qemu/accel/tcg/tcg-all.c
                    {"KVM", VmHypervisorType::Kvm},          // qemu/accel/kvm/kvm-all.c
                    {"HVF", VmHypervisorType::Hvf},          // qemu/accel/hvf/hvf-accel-ops.c
                    {"WHPX", VmHypervisorType::Whpx},        // qemu/target/i386/whpx/whpx-all.c
                }};
        static_assert(std::size(hyperVisorData) == ((int)VmHypervisorType::Max));

        config.hypervisorType = VmHypervisorType::Unknown;
        for (const auto& pair : hyperVisorData) {
            if (pair.first == accel) {
                config.hypervisorType = pair.second;
                break;
            }
        }
        config.numberOfCpuCores = ::cpu_count_cwrap();
        config.cpu_type = ::cpu_type_cwrap();
        return config;
    }

    /**
     * @brief Gets the current run state of the QEMU emulator.
     *
     * @return The emulator's run state.
     */
    EmuRunState getRunState() override { return static_cast<EmuRunState>(runstate_get()); }

    /**
     * @brief Requests a system shutdown with a specified reason.
     *
     * @param reason The reason for the shutdown request.
     */
    void systemShutdownRequest(QemuShutdownCause reason) override {
        qemu_system_shutdown_request((ShutdownCause)reason);
    }

    absl::Status SaveSnapshot(const char* name, const bool overwrite) override {
        ::Error* errp = nullptr;
        if (!::save_snapshot(name, overwrite, /*vmstate=*/nullptr, /*has_devices=*/false,
                             /*devices=*/nullptr, &errp)) {
            return ToStatus(absl::StatusCode::kInternal, &errp);
        }

        return absl::OkStatus();
    }

    absl::Status LoadSnapshot(const char* idOrName, const bool andResume) override {
        ::Error* errp = nullptr;
        if (!::load_snapshot(idOrName, /*vmstate=*/nullptr, /*has_devices=*/false,
                             /*devices=*/nullptr, &errp)) {
            return ToStatus(absl::StatusCode::kInternal, &errp);
        }

        if (andResume) {
            ::load_snapshot_resume(::RUN_STATE_RUNNING);
        }

        return absl::OkStatus();
    }

    void LoadSnapshotResume(const EmuRunState ers) override {
        ::load_snapshot_resume(static_cast<::RunState>(ers));
    }

    absl::Status DeleteSnapshot(const char* idOrName) override {
        ::Error* errp = nullptr;
        if (!::delete_snapshot(idOrName, /*has_devices=*/false, /*devices=*/nullptr, &errp)) {
            return ToStatus(absl::StatusCode::kInternal, &errp);
        }

        return absl::OkStatus();
    }

    absl::Status ListSnapshots(const SnapshotEntrySink sink) override {
        ScopedVmLock lock;
        ::Error* errp = nullptr;
        ::BlockDriverState* bs = ::bdrv_all_find_vmstate_bs(
                /*vmstate_bs=*/nullptr, /*has_devices=*/false, /*devices=*/nullptr, &errp);
        if (!bs) {
            return ToStatus(absl::StatusCode::kInternal, &errp);
        }

        ::QEMUSnapshotInfo* qsi = nullptr;
        const int qsiSize = ::bdrv_snapshot_list(bs, &qsi);
        if (qsiSize < 0) {
            return absl::InternalError(absl::StrCat("bdrv_snapshot_list failed with ", qsiSize));
        }

        for (int i = 0; i < qsiSize; ++i) {
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
    }
};

/**
 * @brief Returns a singleton instance of the QemuVmOperations class.
 *
 * @return A pointer to the QemuVmOperations instance.
 */
VmOperations* VmOperations::qemuVmOperations() {
    static QemuVmOperations instance;
    return &instance;
}

}  // namespace goldfish
}  // namespace android
