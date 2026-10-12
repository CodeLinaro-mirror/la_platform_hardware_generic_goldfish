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

    bool stop() override {
        mIsRunning = false;
        return true;
    }
    bool start() override {
        mIsRunning = true;
        return true;
    }
    void reset() override {}
    void Shutdown() override { mIsRunning = false; }
    bool pause() override {
        mRunState = EmuRunState::Paused;
        RunStateEvents().FireEvent(mRunState);
        return true;
    }
    bool resume() override {
        mRunState = EmuRunState::Running;
        RunStateEvents().FireEvent(mRunState);
        return true;
    }
    bool isRunning() override { return mIsRunning; }
    void setIsRunning(bool isRunning) { mIsRunning = isRunning; }

    VmConfiguration getConfiguration() override { return {VmHypervisorType::None, 1, "fake"}; }
    EmuRunState getRunState() override { return mRunState; }
    void setRunState(EmuRunState runState) {
        mRunState = runState;
        RunStateEvents().FireEvent(mRunState);
    }

    void systemShutdownRequest(QemuShutdownCause reason) override {}

  private:
    bool mIsRunning = true;
    EmuRunState mRunState = EmuRunState::Running;
};

}  // namespace goldfish
}  // namespace android
