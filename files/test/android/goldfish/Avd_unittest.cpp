// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include <gtest/gtest.h>

#include "android/goldfish/avd/Avd.h"

#include <fstream>
#include <iostream>
#include <memory>

#include "aemu/base/ArraySize.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/memory/ScopedPtr.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/ConfigDirs.h"

using android::base::ScopedCPtr;
using android::base::TestSystem;
using android::base::TestTempDir;

namespace android::goldfish {
static fs::path pj(fs::path a, fs::path b) { return a / b; }

void writeToFile(fs::path path, std::string text) {
  std::ofstream iniFile(path, std::ios::trunc);
  iniFile << text;
  iniFile.close();
}

TEST(AvdUtil, path_getAvdSystemPath) {
  TestSystem sys("/home", "/");
  TestTempDir *tmp = sys.getTempRoot();
  tmp->makeSubDir("android_home");
  tmp->makeSubDir(pj("android_home", "sysimg"));
  tmp->makeSubDir(pj("android_home", "avd"));
  tmp->makeSubDir("nothome");

  std::string sdkRoot = pj(tmp->pathString(), "android_home");
  std::string avdConfig = pj(pj(sdkRoot, "avd"), "config.ini");
  sys.envSet("ANDROID_AVD_HOME", sdkRoot);
  EXPECT_EQ(ConfigDirs::getAvdRootDirectory().string(),
            tmp->path() / "android_home");

  // Create an in file for the @q avd.
  writeToFile(pj(sdkRoot, "q.ini"),
              std::string("path=") + pj(sdkRoot, "avd").string());

  // A relative path should be resolved from ANRDOID_AVD_HOME
  writeToFile(avdConfig, "image.sysdir.1=sysimg");

  auto inis = Avd::list();
  EXPECT_EQ(1, inis.size());
}
} // namespace android::goldfish