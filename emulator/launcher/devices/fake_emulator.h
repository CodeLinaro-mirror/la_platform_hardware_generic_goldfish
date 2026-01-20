#include <memory>

#include "android/base/bazel_info.h"
#include "android/cmdline_definitions.h"
#include "android/goldfish/emulator_config.h"
#include "mock_avd.h"

namespace android::goldfish {

class FakeEmulator {
  public:
    FakeEmulator(EmulatorPorts ports, AndroidOptions opts) {
        auto avd = std::make_unique<MockAvd>();

        mMockAvd = avd.get();
        mEmulatorConfig = std::make_unique<EmulatorConfig>(
                std::move(ports), "", ResolvedInputPaths{.launcher_directory=fs::path(android::base::Bazel::RunfilesPath("goldfish+/emulator/launcher")).make_preferred()}, std::move(avd), std::move(opts));
    }

    explicit FakeEmulator(AndroidOptions opts) : FakeEmulator(EmulatorPorts{}, std::move(opts)) {}

    FakeEmulator() : FakeEmulator(AndroidOptions{}) {}

    MockAvd& mock_avd() { return *mMockAvd; }

    const EmulatorConfig& config() { return *mEmulatorConfig; }

  private:
    MockAvd* mMockAvd{nullptr};
    std::unique_ptr<EmulatorConfig> mEmulatorConfig;
};

}  // namespace android::goldfish
