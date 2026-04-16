#include "avd_info_device.h"

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Return;
using ::testing::ReturnRef;

TEST(AvdInfoDeviceTest, Basic) {
    FakeEmulator emu;

    AvdInfoDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), absl_testing::IsOk());
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ::testing::ElementsAre(Eq("-device"), ::testing::HasSubstr("avdstart,")));
}

}  // namespace android::goldfish::test
