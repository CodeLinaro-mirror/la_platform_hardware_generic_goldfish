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

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "emulator/config/test/android/goldfish/fake_hardware_config.h"
#include "sensor_service.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

using ::goldfish::sensors::PhysicalModel;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;

class SensorServiceIncubatingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mHw = android::goldfish::FakeHardwareConfig::GetHwConfig();
        mPhysicalModel = std::make_unique<PhysicalModel>(mHw);
        mService = std::make_unique<SensorServiceIncubatingImpl>(*mPhysicalModel);

        grpc::ServerBuilder builder;
        builder.RegisterService(mService.get());
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &mPort);
        mServer = builder.BuildAndStart();

        mChannel = grpc::CreateChannel("localhost:" + std::to_string(mPort),
                                       grpc::InsecureChannelCredentials());
        mStub = SensorService::NewStub(mChannel);
    }

    void TearDown() override {
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
        mServer->Shutdown(deadline);
        mServer->Wait();
    }

    std::unique_ptr<grpc::ClientContext> getContextWithTimeout(
            std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        auto context = std::make_unique<grpc::ClientContext>();
        std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + timeout;
        context->set_deadline(deadline);
        return context;
    }

    android::goldfish::HardwareConfig mHw;
    std::unique_ptr<PhysicalModel> mPhysicalModel;
    std::unique_ptr<SensorServiceIncubatingImpl> mService;
    std::unique_ptr<grpc::Server> mServer;
    std::shared_ptr<grpc::Channel> mChannel;
    std::unique_ptr<SensorService::Stub> mStub;
    int mPort;
};

TEST_F(SensorServiceIncubatingTest, ReceivePhysicalStateEvents) {
    auto context = getContextWithTimeout();
    Empty request;
    auto reader = mStub->receivePhysicalStateEvents(context.get(), request);

    // Give it a bit of time to settle the connection.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Triggering target state change
    float val = 1.0f;
    mPhysicalModel->SetPhysicalParameterValue(::goldfish::sensors::PhysicalParameter::POSITION,
                                              &val, 1, ::PhysicalInterpolation::kStep);

    PhysicalStateEvent event;
    // We expect at least two events: CHANGING and TARGET_STATE_CHANGED
    ASSERT_TRUE(reader->Read(&event)) << "Failed to read first event";
    EXPECT_EQ(event.event(), PhysicalStateEvent::STATE_PHYSICAL_STATE_CHANGING);

    ASSERT_TRUE(reader->Read(&event)) << "Failed to read second event";
    EXPECT_EQ(event.event(), PhysicalStateEvent::STATE_TARGET_STATE_CHANGED);

    // Clean up
    context->TryCancel();
}

TEST_F(SensorServiceIncubatingTest, SetGetPhysicalModel) {
    // Ensure time is set
    mPhysicalModel->SetCurrentTime(1000);

    PhysicalModelValue request;
    request.set_target(PhysicalModelValue::PHYSICAL_TYPE_POSITION);
    request.mutable_value()->add_data(10.0f);
    request.mutable_value()->add_data(20.0f);
    request.mutable_value()->add_data(30.0f);
    request.set_interpolation(PhysicalModelValue::INTERPOLATION_STEP);

    Empty reply;
    auto context = getContextWithTimeout();
    Status status = mStub->setPhysicalModel(context.get(), request, &reply);
    ASSERT_TRUE(status.ok()) << status.error_message();

    mPhysicalModel->SetCurrentTime(2000);

    PhysicalModelValue getRequest;
    getRequest.set_target(PhysicalModelValue::PHYSICAL_TYPE_POSITION);
    getRequest.set_value_type(PhysicalModelValue::PARAMETER_VALUE_TYPE_TARGET);

    PhysicalModelValue getReply;
    auto getContext = getContextWithTimeout();
    status = mStub->getPhysicalModel(getContext.get(), getRequest, &getReply);
    ASSERT_TRUE(status.ok()) << status.error_message();

    EXPECT_EQ(getReply.target(), PhysicalModelValue::PHYSICAL_TYPE_POSITION);
    ASSERT_EQ(getReply.value().data_size(), 3);
    EXPECT_NEAR(getReply.value().data(0), 10.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(1), 20.0f, 0.1f);
    EXPECT_NEAR(getReply.value().data(2), 30.0f, 0.1f);
}

TEST_F(SensorServiceIncubatingTest, ReceivePhysicalModelEvents) {
    auto context = getContextWithTimeout();
    PhysicalModelValue request;
    request.set_target(PhysicalModelValue::PHYSICAL_TYPE_POSITION);
    request.set_value_type(PhysicalModelValue::PARAMETER_VALUE_TYPE_TARGET);

    auto reader = mStub->receivePhysicalModelEvents(context.get(), request);

    // Give it a bit of time to settle the connection.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Triggering target state change
    float val[3] = {1.0f, 2.0f, 3.0f};
    mPhysicalModel->SetPhysicalParameterValue(::goldfish::sensors::PhysicalParameter::POSITION, val,
                                              3, ::PhysicalInterpolation::kStep);

    PhysicalModelValue event;
    // We expect one event for TARGET_STATE_CHANGED
    ASSERT_TRUE(reader->Read(&event)) << "Failed to read event";
    EXPECT_EQ(event.target(), PhysicalModelValue::PHYSICAL_TYPE_POSITION);
    ASSERT_EQ(event.value().data_size(), 3);
    EXPECT_NEAR(event.value().data(0), 1.0f, 0.1f);
    EXPECT_NEAR(event.value().data(1), 2.0f, 0.1f);
    EXPECT_NEAR(event.value().data(2), 3.0f, 0.1f);

    // Clean up
    context->TryCancel();
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
