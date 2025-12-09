#pragma once

#include "android/goldfish/vm_interface.h"

namespace android {
namespace goldfish {

/**
 * A fake VmOperations for testing.
 */
class FakeVmOperations : public VmOperations {
  public:
    bool stop() override {
        mIsRunning = false;
        return true;
    }
    bool start() override {
        mIsRunning = true;
        return true;
    }
    void reset() override {}
    void shutdown() override { mIsRunning = false; }
    bool pause() override {
        mRunState = EmuRunState::Paused;
        return true;
    }
    bool resume() override {
        mRunState = EmuRunState::Running;
        return true;
    }
    bool isRunning() override { return mIsRunning; }
    void setIsRunning(bool isRunning) { mIsRunning = isRunning; }

    VmConfiguration getConfiguration() override { return {VmHypervisorType::None, 1, "fake"}; }
    EmuRunState getRunState() override { return mRunState; }
    void setRunState(EmuRunState runState) { mRunState = runState; }

    void systemShutdownRequest(QemuShutdownCause reason) override {}

  private:
    bool mIsRunning = true;
    EmuRunState mRunState = EmuRunState::Running;
};

}  // namespace goldfish
}  // namespace android
