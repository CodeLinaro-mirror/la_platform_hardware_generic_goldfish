// Copyright 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/crashreport/crash_detectors.h"

#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
// #include "android/console.h"

#include <gtest/gtest.h>

using namespace android::base;
using namespace android::crashreport;

class AlwaysCrash : public StatefulHangdetector {
  public:
    bool Check() { return true; }
};

TEST(CrashDetectorsTest, timeoutProperly) {
    TestSystem testSys("foo");
    TimedHangDetector t(15, new AlwaysCrash());
    EXPECT_FALSE(t.Check());
    testSys.setUnixTimeUs(1000000);
    EXPECT_TRUE(t.Check());
}

TEST(CrashDetectorsTest, heartBeatDetector_detectsBootFailure) {
    TestSystem testSys("foo");
    int beatCount = 0;
    HeartBeatDetector hb([&beatCount] { return beatCount; });
    EXPECT_FALSE(hb.Check());

    // First time boot so should be ok
    EXPECT_FALSE(hb.Check());

    beatCount++;
    // Heartbeat.. no problem!
    EXPECT_FALSE(hb.Check());
    // No heartbeat.. trouble!
    EXPECT_TRUE(hb.Check());
}
