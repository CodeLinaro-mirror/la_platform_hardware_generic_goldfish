#include <memory>

#include "android/base/bazel_info.h"
#include "android/cmdline_definitions.h"
#include "android/goldfish/emulator_config.h"
#include "mock_avd.h"

namespace android::goldfish {

class FakeEmulator {
  public:
    FakeEmulator(EmulatorPorts ports, AndroidOptions opts)
            : mPorts(std::move(ports))
            , mOpts(std::move(opts))
            , mResolvedPaths{.launcher_directory = fs::path(android::base::Bazel::RunfilesPath(
                                                                    "goldfish+/emulator/launcher"))
                                                           .make_preferred()}
            , mMockAvd(std::make_unique<MockAvd>()) {
        mEmulatorConfig = std::make_unique<EmulatorConfig>(mPorts, mChardevEndpoints, mMetricsConfig,
                                                           mResolvedPaths, *mMockAvd, mOpts);
    }

    explicit FakeEmulator(AndroidOptions opts) : FakeEmulator(EmulatorPorts{}, std::move(opts)) {}

    FakeEmulator() : FakeEmulator(AndroidOptions{}) {}

    MockAvd& mock_avd() { return *mMockAvd; }

    const EmulatorConfig& config() { return *mEmulatorConfig; }

  private:
    EmulatorPorts mPorts;
    AndroidOptions mOpts;
    ChardevEndpoints mChardevEndpoints;
    MetricsConfig mMetricsConfig;

    ResolvedInputPaths mResolvedPaths;
    std::unique_ptr<MockAvd> mMockAvd;

    std::unique_ptr<EmulatorConfig> mEmulatorConfig;
};

}  // namespace android::goldfish
