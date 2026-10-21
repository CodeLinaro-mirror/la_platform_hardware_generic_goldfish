#include <memory>

#include "android/base/bazel_info.h"
#include "android/cmdline_definitions.h"
#include "emulator_config.h"
#include "mock_avd.h"

namespace android::goldfish {

class FakeEmulator {
  public:
    FakeEmulator(EmulatorPorts ports, AndroidOptions opts)
            : mPorts(std::move(ports))
            , mOpts(std::move(opts))
            , mEmulatorPaths{.launcher_directory = fs::path(android::base::Bazel::RunfilesPath(
                                                                    "goldfish+/emulator/launcher"))
                                                           .make_preferred(),
                                                          }
            , mMockAvd(std::make_unique<MockAvd>()) {
        ON_CALL(*mMockAvd, GetSystemImagePaths())
                .WillByDefault(testing::ReturnRef(mSystemImagePaths));
        mEmulatorConfig =
                std::make_unique<EmulatorConfig>(mPorts, mChardevEndpoints, mMetricsConfig,
                                                 mUserPaths, mEmulatorPaths, *mMockAvd, mOpts);
    }

    explicit FakeEmulator(AndroidOptions opts) : FakeEmulator(EmulatorPorts{}, std::move(opts)) {}

    FakeEmulator() : FakeEmulator(AndroidOptions{}) {}

    MockAvd& mock_avd() { return *mMockAvd; }

    EmulatorPaths& emulator_paths() { return mEmulatorPaths; }

    UserPaths& user_paths() { return mUserPaths; }

    SystemImagePaths& system_image_paths() { return mSystemImagePaths; }

    const EmulatorConfig& config() { return *mEmulatorConfig; }

  private:
    EmulatorPorts mPorts;
    AndroidOptions mOpts;
    ChardevEndpoints mChardevEndpoints;
    MetricsConfig mMetricsConfig;

    EmulatorPaths mEmulatorPaths;
    UserPaths mUserPaths;
    SystemImagePaths mSystemImagePaths;
    std::unique_ptr<MockAvd> mMockAvd;

    std::unique_ptr<EmulatorConfig> mEmulatorConfig;
};

}  // namespace android::goldfish
