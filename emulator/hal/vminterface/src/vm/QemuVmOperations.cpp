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

#include "android/goldfish/vm/VmInterface.h"
#include "host-common/VmLock.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "system/runstate.h"
#include "vm/qemu-machine-info.h"
}
// IWYU pragma: end_keep
// clang-format on

// Remove leaked shutdown redefinition from external/qemu/include/sysemu/os-win32.h
#undef shutdown

namespace android {
namespace goldfish {

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
    void shutdown() override {
        ScopedVmLock lock;
        vm_shutdown();
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
