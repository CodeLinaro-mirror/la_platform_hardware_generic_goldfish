#pragma once

#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "android/goldfish/vm_interface.h"

namespace android {
namespace goldfish {

/**
 * A fake VmOperations for testing.
 */
class FakeVmOperations : public VmOperations {
  public:
    explicit FakeVmOperations(::goldfish::async::EventLoop* loop) : VmOperations(loop) {}

    absl::Status Stop() override { return Pause(); }
    absl::Status Start() override { return Resume(); }
    absl::Status Reset() override {
        if (run_state == EmuRunState::Shutdown || run_state == EmuRunState::SaveVm ||
            run_state == EmuRunState::RestoreVm) {
            return absl::FailedPreconditionError("Cannot reset VM in current state");
        }
        ++reset_count;
        SetRunState(EmuRunState::Running);
        return absl::OkStatus();
    }
    absl::Status Shutdown() override {
        if (run_state == EmuRunState::SaveVm || run_state == EmuRunState::RestoreVm) {
            return absl::FailedPreconditionError("Cannot shut down VM during snapshot");
        }
        ++shutdown_count;
        SetRunState(EmuRunState::Shutdown);
        return absl::OkStatus();
    }
    absl::Status Pause() override {
        if (run_state == EmuRunState::Paused) {
            return absl::OkStatus();
        }
        if (run_state != EmuRunState::Running && run_state != EmuRunState::Suspended) {
            return absl::FailedPreconditionError("Cannot pause VM in current state");
        }
        SetRunState(EmuRunState::Paused);
        return absl::OkStatus();
    }
    absl::Status Resume() override {
        if (run_state == EmuRunState::Running) {
            return absl::OkStatus();
        }
        if (run_state != EmuRunState::Paused && run_state != EmuRunState::Suspended) {
            return absl::FailedPreconditionError("Cannot resume VM in current state");
        }
        SetRunState(EmuRunState::Running);
        return absl::OkStatus();
    }
    bool IsRunning() override { return run_state == EmuRunState::Running; }

    VmConfiguration GetConfiguration() override { return configuration; }
    EmuRunState GetRunState() override { return run_state; }
    void SetRunState(EmuRunState state) {
        run_state = state;
        RunStateEvents().FireEvent(run_state);
    }

    void SystemShutdownRequest(QemuShutdownCause /*reason*/) override {}
    void LoadSnapshotResume(EmuRunState /*state*/) override {}

    absl::Status SaveSnapshot(std::string name, bool /*overwrite*/) override {
        if (fail_save) {
            return absl::InternalError("Failed to write snapshot to disk");
        }
        entries[name] = SnapshotEntry{
            .id = name,
            .name = name,
            .timestamp = absl::FromUnixSeconds(1700000000),
        };
        return absl::OkStatus();
    }

    absl::Status LoadSnapshot(std::string id_or_name, bool /*and_resume*/) override {
        if (fail_load) {
            return absl::InternalError("Failed to read snapshot from disk");
        }
        if (!entries.contains(id_or_name)) {
            return absl::NotFoundError("Snapshot does not exist");
        }
        last_loaded_snapshot = std::move(id_or_name);
        return absl::OkStatus();
    }

    absl::Status DeleteSnapshot(std::string id_or_name) override {
        if (entries.erase(id_or_name) == 0) {
            return absl::NotFoundError("Snapshot does not exist");
        }
        return absl::OkStatus();
    }

    absl::Status ListSnapshots(SnapshotEntrySink sink) override {
        for (const auto& [id, entry] : entries) {
            sink(entry);
        }
        return absl::OkStatus();
    }

    bool fail_save = false;
    bool fail_load = false;
    int reset_count = 0;
    int shutdown_count = 0;
    EmuRunState run_state = EmuRunState::Running;
    VmConfiguration configuration = {
        .hypervisor_type = VmHypervisorType::Hvf,
        .number_of_cpu_cores = 4,
        .cpu_type = "arm",
    };
    std::string last_loaded_snapshot;
    absl::flat_hash_map<std::string, SnapshotEntry> entries;
};

}  // namespace goldfish
}  // namespace android
