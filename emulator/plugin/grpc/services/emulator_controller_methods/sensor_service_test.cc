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
#include "sensor_service.h"

#include <chrono>
#include <memory>
#include <vector>

#include "android/goldfish/fake_hardware_config.h"
#include "test/GrpcServiceTest.h"
#include "emulator_controller.grpc.pb.h"

namespace android::emulation::control {

using ::goldfish::sensors::PhysicalModel;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;

class SensorServiceWrapper : public EmulatorController::Service {
  public:
    SensorServiceWrapper(SensorServiceImpl& sensorService) : mSensorService(sensorService) {}

    Status setSensor(ServerContext* /*context*/, const SensorValue* request,
                     Empty* /*reply*/) override {
        return mSensorService.setSensor(*request);
    }
    Status getSensor(ServerContext* /*context*/, const SensorValue* request,
                     SensorValue* reply) override {
        return mSensorService.getSensor(*request, reply);
    }
    Status setPhysicalModel(ServerContext* /*context*/, const PhysicalModelValue* request,
                            Empty* /*reply*/) override {
        return mSensorService.setPhysicalModel(*request);
    }
    Status getPhysicalModel(ServerContext* /*context*/, const PhysicalModelValue* request,
                            PhysicalModelValue* reply) override {
        return mSensorService.getPhysicalModel(*request, reply);
    }

  private:
    SensorServiceImpl& mSensorService;
};

class SensorServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        mHw = android::goldfish::FakeHardwareConfig::GetHwConfig();
        mPhysicalModel = std::make_unique<PhysicalModel>(mHw);
        mSensorService = std::make_unique<SensorServiceImpl>(*mPhysicalModel);
        mServiceWrapper = std::make_unique<SensorServiceWrapper>(*mSensorService);

        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return mServiceWrapper.get(); }

  protected:
    android::goldfish::HardwareConfig mHw;
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    std::unique_ptr<SensorServiceImpl> mSensorService;
    std::unique_ptr<SensorServiceWrapper> mServiceWrapper;
};

TEST_F(SensorServiceTest, SetGetPhysicalModelRotation) {
    PhysicalModelValue request;
    request.set_target(PhysicalModelValue::ROTATION);
    request.mutable_value()->add_data(10.0f);
    request.mutable_value()->add_data(20.0f);
    request.mutable_value()->add_data(30.0f);

    Empty reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->setPhysicalModel(context.get(), request, &reply));

    PhysicalModelValue getRequest;
    getRequest.set_target(PhysicalModelValue::ROTATION);
    PhysicalModelValue getReply;
    auto getContext = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getPhysicalModel(getContext.get(), getRequest, &getReply));

    EXPECT_EQ(getReply.target(), PhysicalModelValue::ROTATION);
    EXPECT_EQ(getReply.status(), PhysicalModelValue::OK);
    ASSERT_EQ(getReply.value().data_size(), 3);
    EXPECT_NEAR(getReply.value().data(0), 10.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(1), 20.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(2), 30.0f, 0.1f);
}

TEST_F(SensorServiceTest, SetGetPhysicalModelPosition) {
    PhysicalModelValue request;
    request.set_target(PhysicalModelValue::POSITION);
    request.mutable_value()->add_data(1.0f);
    request.mutable_value()->add_data(2.0f);
    request.mutable_value()->add_data(3.0f);

    Empty reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->setPhysicalModel(context.get(), request, &reply));

    PhysicalModelValue getRequest;
    getRequest.set_target(PhysicalModelValue::POSITION);
    PhysicalModelValue getReply;
    auto getContext = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->getPhysicalModel(getContext.get(), getRequest, &getReply));

    EXPECT_EQ(getReply.target(), PhysicalModelValue::POSITION);
    EXPECT_EQ(getReply.status(), PhysicalModelValue::OK);
    ASSERT_EQ(getReply.value().data_size(), 3);
    EXPECT_NEAR(getReply.value().data(0), 1.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(1), 2.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(2), 3.0f, 0.1f);
}

}  // namespace android::emulation::control
