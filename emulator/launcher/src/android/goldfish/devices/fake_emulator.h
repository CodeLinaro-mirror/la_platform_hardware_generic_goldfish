#include <android/cmdline-definitions.h>
#include <memory>

#include "android/goldfish/emulator_config.h"
#include "mock_avd.h"

namespace android::goldfish {

class FakeEmulator {
  public:
    FakeEmulator(EmulatorPorts ports, AndroidOptions opts) {
        auto avd = std::make_unique<MockAvd>();

        mMockAvd = avd.get();
        mEmulatorConfig = std::make_unique<EmulatorConfig>(std::move(ports), "", ResolvedInputPaths{}, std::move(avd), std::move(opts));
    }

    explicit FakeEmulator(AndroidOptions opts) : FakeEmulator(EmulatorPorts{}, std::move(opts)) {}

    FakeEmulator() : FakeEmulator(AndroidOptions{}) {}

    MockAvd& mock_avd() {
        return *mMockAvd;
    }

    const EmulatorConfig &config() {
        return *mEmulatorConfig;
    }

  private:
    MockAvd* mMockAvd{nullptr};
    std::unique_ptr<EmulatorConfig> mEmulatorConfig;
};

}  // namespace android::goldfish