// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <memory>

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/fake-avd.h"
namespace android::goldfish {

using android::base::System;
using android::base::TestSystem;
using android::base::TestTempDir;

class [[deprecated("This class is deprecated, use FakeAvd instead")]] AvdTest
    : public ::testing::Test {
  public:
    AvdTest() {
        TestSystem sys("/home", "/");
        TestTempDir* tmp = sys.getTempRoot();
        tmp->makeSubDir("android_home");
        tmp->makeSubDir(pj("android_home", "avd"));
        createTestAvd(sys, tmp, "android-30");

        ASSERT_OK_AND_ASSIGN(mAvd, Avd::fromName("test_avd"));
    }

    Avd* avd() { return mAvd.get(); }

  private:
    fs::path pj(fs::path a, fs::path b) { return a / b; }

    void writeToFile(fs::path path, std::string text) {
        std::ofstream iniFile(path, std::ios::trunc);
        iniFile << text;
        iniFile.close();
    }

    void createTestAvd(TestSystem& sys, TestTempDir* tmp, const std::string& targetString) {
        std::string sdkRoot = pj(tmp->pathString(), "android_home");
        std::string avdConfig = pj(pj(sdkRoot, "avd"), "config.ini");
        sys.envSet("ANDROID_AVD_HOME", sdkRoot);

        // Create an ini file for the test AVD
        writeToFile(pj(sdkRoot, "test_avd.ini"),
                    std::string("path=") + pj(sdkRoot, "avd").string());

        // Set the 'target' property in the config.ini file
        writeToFile(avdConfig, "target=" + targetString);
    }
    std::unique_ptr<Avd> mAvd;
};
}  // namespace android::goldfish