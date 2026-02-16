// Copyright (C) 2026 The Android Open Source Project
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
#include "android/emulation/control/incubating/sensor_service_incubating.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <memory>

#include "emulator/config/test/android/goldfish/fake_hardware_config.h"
#include "sensor_service.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

using ::goldfish::sensors::PhysicalModel;
using ::grpc::Status;

class SensorServiceIncubatingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mHw = android::goldfish::FakeHardwareConfig::GetHwConfig();
        mPhysicalModel = std::make_unique<PhysicalModel>(mHw);
        mService = std::make_unique<SensorServiceIncubatingImpl>(*mPhysicalModel);
    }

    android::goldfish::HardwareConfig mHw;
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    std::unique_ptr<SensorServiceIncubatingImpl> mService;
};

TEST_F(SensorServiceIncubatingTest, Skeleton) {
    EXPECT_TRUE(true);
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
