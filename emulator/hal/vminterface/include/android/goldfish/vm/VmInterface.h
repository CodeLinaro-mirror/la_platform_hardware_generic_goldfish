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
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"

namespace android {
namespace goldfish {

// Enumeration of various causes for shutdown.
enum class QemuShutdownCause {
    None,                ///< No shutdown request pending.
    HostError,           ///< An error prevents further use of guest.
    HostQmpQuit,         ///< Reaction to the QMP command 'quit'.
    HostQmpSystemReset,  ///< Reaction to the QMP command 'system_reset'.
    HostSignal,          ///< Reaction to a signal, such as SIGINT.
    HostUi,              ///< Reaction to UI event, like window close.
    GuestShutdown,   ///< Guest shutdown/suspend request, via ACPI or other hardware-specific means.
    GuestReset,      ///< Guest reset request, and command line turns that into a shutdown.
    GuestPanic,      ///< Guest panicked, and command line turns that into a shutdown.
    SubsystemReset,  ///< Partial guest reset that does not trigger QMP events and ignores
                     ///< --no-reboot.
    SnapshotLoad,    ///< A snapshot is being loaded by the record & replay subsystem.
    Max,             ///< Maximum value for this enum.
};

template <typename Sink>
void AbslStringify(Sink& sink, QemuShutdownCause e) {
    switch (e) {
        case QemuShutdownCause::None:
            absl::Format(&sink, "None");
            break;
        case QemuShutdownCause::HostError:
            absl::Format(&sink, "HostError");
            break;
        case QemuShutdownCause::HostQmpQuit:
            absl::Format(&sink, "HostQmpQuit");
            break;
        case QemuShutdownCause::HostQmpSystemReset:
            absl::Format(&sink, "HostQmpSystemReset");
            break;
        case QemuShutdownCause::HostSignal:
            absl::Format(&sink, "HostSignal");
            break;
        case QemuShutdownCause::HostUi:
            absl::Format(&sink, "HostUi");
            break;
        case QemuShutdownCause::GuestShutdown:
            absl::Format(&sink, "GuestShutdown");
            break;
        case QemuShutdownCause::GuestReset:
            absl::Format(&sink, "GuestReset");
            break;
        case QemuShutdownCause::GuestPanic:
            absl::Format(&sink, "GuestPanic");
            break;
        case QemuShutdownCause::SubsystemReset:
            absl::Format(&sink, "SubsystemReset");
            break;
        case QemuShutdownCause::SnapshotLoad:
            absl::Format(&sink, "SnapshotLoad");
            break;
        case QemuShutdownCause::Max:
            absl::Format(&sink, "Max");
            break;
        default:
            absl::Format(&sink, "Unknown QemuShutdownCause (%d)", static_cast<int>(e));
            break;
    }
}

// VM Hypervisor types
enum class VmHypervisorType {
    Unknown = 0,  ///< Unknown hypervisor type.
    None,         ///< No hypervisor (tiny code generator).
    Kvm,          ///< KVM hypervisor.
    Hvf,          ///< HVF hypervisor.
    Whpx,         ///< WHPX hypervisor.
    Max,          ///< Maximum value for this enum (unused)
};

template <typename Sink>
void AbslStringify(Sink& sink, VmHypervisorType e) {
    switch (e) {
        case VmHypervisorType::Unknown:
            absl::Format(&sink, "Unknown");
            break;
        case VmHypervisorType::None:
            absl::Format(&sink, "None");
            break;
        case VmHypervisorType::Kvm:
            absl::Format(&sink, "Kvm");
            break;
        case VmHypervisorType::Hvf:
            absl::Format(&sink, "Hvf");
            break;
        case VmHypervisorType::Whpx:
            absl::Format(&sink, "Whpx");
            break;
        case VmHypervisorType::Max:
            absl::Format(&sink, "Max");
            break;
        default:
            absl::Format(&sink, "Unknown VmHypervisorType (%d)", static_cast<int>(e));
            break;
    }
}

// VM Configuration
struct VmConfiguration {
    VmHypervisorType hypervisorType;  ///< Type of hypervisor.
    int32_t numberOfCpuCores;         ///< Number of CPU cores.
    const char* cpu_type;             ///< Type of the cpu
};

template <typename Sink>
void AbslStringify(Sink& sink, const VmConfiguration& config) {
    absl::Format(&sink,
                 "VmConfiguration { hypervisorType: %s, numberOfCpuCores: %d, cpu_type: %s }",
                 config.hypervisorType, config.numberOfCpuCores, config.cpu_type);
}

/**
 * @brief Enumeration of emulator run states.
 *
 * Defines the different states the emulator can be in during its lifecycle.
 */
enum class EmuRunState {
    Debug = 0,      ///< QEMU is running on a debugger.
    InMigrate,      ///< Guest is paused waiting for an incoming migration.
    InternalError,  ///< An internal error that prevents further guest execution has occurred.
    IoError,  ///< The last I/O operation has failed and the device is configured to pause on I/O
              ///< errors.
    Paused,   ///< Guest has been paused via the 'stop' command.
    PostMigrate,    ///< Guest is paused following a successful 'migrate'.
    PreLaunch,      ///< QEMU was started with -S and guest has not started.
    FinishMigrate,  ///< Guest is paused to finish the migration process.
    RestoreVm,      ///< Guest is paused to restore VM state.
    Running,        ///< Guest is actively running.
    SaveVm,         ///< Guest is paused to save the VM state.
    Shutdown,       ///< Guest is shut down (and -no-shutdown is in use).
    Suspended,      ///< Guest is suspended (ACPI S3).
    Watchdog,       ///< The watchdog action is configured to pause and has been triggered.
    GuestPanicked,  ///< Guest has been panicked as a result of guest OS panic.
    Colo,           ///< Guest is paused to save/restore VM state under colo checkpoint.
    Max,            ///< Maximum value for this enum.
};

template <typename Sink>
void AbslStringify(Sink& sink, EmuRunState e) {
    switch (e) {
        case EmuRunState::Debug:
            absl::Format(&sink, "Debug");
            break;
        case EmuRunState::InMigrate:
            absl::Format(&sink, "InMigrate");
            break;
        case EmuRunState::InternalError:
            absl::Format(&sink, "InternalError");
            break;
        case EmuRunState::IoError:
            absl::Format(&sink, "IoError");
            break;
        case EmuRunState::Paused:
            absl::Format(&sink, "Paused");
            break;
        case EmuRunState::PostMigrate:
            absl::Format(&sink, "PostMigrate");
            break;
        case EmuRunState::PreLaunch:
            absl::Format(&sink, "PreLaunch");
            break;
        case EmuRunState::FinishMigrate:
            absl::Format(&sink, "FinishMigrate");
            break;
        case EmuRunState::RestoreVm:
            absl::Format(&sink, "RestoreVm");
            break;
        case EmuRunState::Running:
            absl::Format(&sink, "Running");
            break;
        case EmuRunState::SaveVm:
            absl::Format(&sink, "SaveVm");
            break;
        case EmuRunState::Shutdown:
            absl::Format(&sink, "Shutdown");
            break;
        case EmuRunState::Suspended:
            absl::Format(&sink, "Suspended");
            break;
        case EmuRunState::Watchdog:
            absl::Format(&sink, "Watchdog");
            break;
        case EmuRunState::GuestPanicked:
            absl::Format(&sink, "GuestPanicked");
            break;
        case EmuRunState::Colo:
            absl::Format(&sink, "Colo");
            break;
        case EmuRunState::Max:
            absl::Format(&sink, "Max");
            break;
        default:
            absl::Format(&sink, "Unknown EmuRunState (%d)", static_cast<int>(e));
            break;
    }
}

/**
 * @brief Interface for performing operations on a virtual machine.
 *
 * This interface provides methods to control and manage the lifecycle of a VM,
 * including starting, stopping, pausing, resuming, and querying its configuration.
 */
class VmOperations {
  public:
    /**
     * @brief Virtual destructor for the VmOperations interface.
     */
    virtual ~VmOperations() = default;

    /**
     * @brief Stops the virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    virtual bool stop() = 0;

    /**
     * @brief Starts the virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    virtual bool start() = 0;

    /**
     * @brief Resets the virtual machine.
     */
    virtual void reset() = 0;

    /**
     * @brief Shuts down the virtual machine.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Pauses the virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    virtual bool pause() = 0;

    /**
     * @brief Resumes the virtual machine.
     *
     * @return True if the operation was successful, false otherwise.
     */
    virtual bool resume() = 0;

    /**
     * @brief Checks if the virtual machine is currently running.
     *
     * @return True if the VM is running, false otherwise.
     */
    virtual bool isRunning() = 0;

    /**
     * @brief Gets the configuration of the virtual machine.
     *
     * @return The VM's configuration.
     */
    virtual VmConfiguration getConfiguration() = 0;

    /**
     * @brief Gets the current run state of the emulator.
     *
     * @return The emulator's run state.
     */
    virtual EmuRunState getRunState() = 0;

    /**
     * @brief Requests a system shutdown with a specified reason.
     *
     * @param reason The reason for the shutdown request.
     */
    virtual void systemShutdownRequest(QemuShutdownCause reason) = 0;

    static VmOperations* qemuVmOperations();
};

}  // namespace goldfish
}  // namespace android
