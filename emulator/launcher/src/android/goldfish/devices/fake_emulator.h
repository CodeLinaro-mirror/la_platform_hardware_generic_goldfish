#include <android/cmdline-definitions.h>
#include <memory>

#include "android/goldfish/emulator_config.h"
#include "mock_avd.h"

namespace android::goldfish {

class FakeEmulator {
  public:
    explicit FakeEmulator(AndroidOptions opts) {
        auto avd = std::make_unique<MockAvd>();

        mMockAvd = avd.get();
        mEmulatorConfig = std::make_unique<EmulatorConfig>("", ResolvedInputPaths{}, std::move(avd), std::move(opts));
    }

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