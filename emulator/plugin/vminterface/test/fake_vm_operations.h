#pragma once

#include "android/goldfish/vm_interface.h"

namespace android {
namespace goldfish {

/**
 * A fake VmOperations for testing.
 */
class FakeVmOperations : public VmOperations {
  public:
    explicit FakeVmOperations(::goldfish::async::EventLoop* loop) : VmOperations(loop) {}

    bool Stop() override {
        is_running_ = false;
        return true;
    }
    bool Start() override {
        is_running_ = true;
        return true;
    }
    void Reset() override {}
    void Shutdown() override { is_running_ = false; }
    bool Pause() override {
        run_state_ = EmuRunState::Paused;
        RunStateEvents().FireEvent(run_state_);
        return true;
    }
    bool Resume() override {
        run_state_ = EmuRunState::Running;
        RunStateEvents().FireEvent(run_state_);
        return true;
    }
    bool IsRunning() override { return is_running_; }
    void SetIsRunning(bool is_running) { is_running_ = is_running; }

    VmConfiguration GetConfiguration() override { return {VmHypervisorType::None, 1, "fake"}; }
    EmuRunState GetRunState() override { return run_state_; }
    void SetRunState(EmuRunState run_state) {
        run_state_ = run_state;
        RunStateEvents().FireEvent(run_state_);
    }

    void SystemShutdownRequest(QemuShutdownCause /*reason*/) override {}

  private:
    bool is_running_ = true;
    EmuRunState run_state_ = EmuRunState::Running;
};

}  // namespace goldfish
}  // namespace android
