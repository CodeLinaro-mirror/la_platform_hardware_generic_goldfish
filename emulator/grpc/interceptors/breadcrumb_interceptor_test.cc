// Copyright 2026 The Android Open Source Project
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
#include "android/control/interceptor/breadcrumb_interceptor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

#include "absl/time/time.h"
#include "grpc_diagnostic.pb.h"

#include "android/base/abseil_clock.h"
#include "android/base/testing/TestClock.h"
#include "android/crashreport/binary_annotation.h"
#include "android/crashreport/thread.h"
#include "emulator_controller.pb.h"

namespace android::control::interceptor {

using namespace testing;
using namespace grpc::experimental;

class MockInterceptorBatchMethods : public grpc::experimental::InterceptorBatchMethods {
  public:
    MOCK_METHOD(bool, QueryInterceptionHookPoint, (grpc::experimental::InterceptionHookPoints),
                (override));
    MOCK_METHOD(void, Proceed, (), (override));
    MOCK_METHOD(void, Hijack, (), (override));
    MOCK_METHOD(grpc::ByteBuffer*, GetSerializedSendMessage, (), (override));
    MOCK_METHOD(const void*, GetSendMessage, (), (override));
    MOCK_METHOD(void, ModifySendMessage, (const void*), (override));
    MOCK_METHOD(bool, GetSendMessageStatus, (), (override));
    MOCK_METHOD((std::multimap<grpc::string, grpc::string>*), GetSendInitialMetadata, (),
                (override));
    MOCK_METHOD(grpc::Status, GetSendStatus, (), (override));
    MOCK_METHOD(void, ModifySendStatus, (const grpc::Status&), (override));
    MOCK_METHOD((std::multimap<grpc::string, grpc::string>*), GetSendTrailingMetadata, (),
                (override));
    MOCK_METHOD(void*, GetRecvMessage, (), (override));
    MOCK_METHOD((std::multimap<grpc::string_ref, grpc::string_ref>*), GetRecvInitialMetadata, (),
                (override));
    MOCK_METHOD(grpc::Status*, GetRecvStatus, (), (override));
    MOCK_METHOD((std::multimap<grpc::string_ref, grpc::string_ref>*), GetRecvTrailingMetadata, (),
                (override));
    MOCK_METHOD((std::unique_ptr<grpc::ChannelInterface>), GetInterceptedChannel, (), (override));
    MOCK_METHOD(void, FailHijackedRecvMessage, (), (override));
    MOCK_METHOD(void, FailHijackedSendMessage, (), (override));
};

std::vector<GrpcBreadcrumb> GetAllCrumbs() {
    std::vector<GrpcBreadcrumb> result;
    BreadcrumbInterceptor::GetLogForTesting()->ForEach([&](const GrpcBreadcrumb& msg) {
        result.push_back(msg);
        return true;
    });
    return result;
}

class BreadcrumbInterceptorTest : public ::testing::Test {
  public:
    void SetUp() override {
        mTestClock = std::make_unique<android::base::TestClock>();
        mClockPtr = mTestClock.get();
        android::base::IClock::Set(std::move(mTestClock));
    }

    void TearDown() override {
        android::base::IClock::Set(std::make_unique<android::base::AbseilClock>());
    }

    android::base::TestClock* mClockPtr;
    std::unique_ptr<android::base::TestClock> mTestClock;
};

TEST_F(BreadcrumbInterceptorTest, LogsStartOnCreation) {
    auto factory = std::make_unique<BreadcrumbInterceptorFactory>();

    {
        auto interceptor = std::unique_ptr<grpc::experimental::Interceptor>(
                factory->CreateClientInterceptor(nullptr));
    }

    auto crumbs = GetAllCrumbs();

    ASSERT_GE(crumbs.size(), 2);
    // The last two should be START and END_OF_CALL for the interceptor we just destroyed.
    EXPECT_EQ(crumbs[crumbs.size() - 2].phase(), GrpcBreadcrumb::START);
    EXPECT_EQ(crumbs.back().phase(), GrpcBreadcrumb::END_OF_CALL);
}

TEST_F(BreadcrumbInterceptorTest, HandlesLargePayloadsByDroppingDetail) {
    auto factory = std::make_unique<BreadcrumbInterceptorFactory>();
    auto interceptor = std::unique_ptr<grpc::experimental::Interceptor>(
            factory->CreateClientInterceptor(nullptr));

    MockInterceptorBatchMethods methods;
    EXPECT_CALL(methods, QueryInterceptionHookPoint(
                                 grpc::experimental::InterceptionHookPoints::PRE_SEND_MESSAGE))
            .WillRepeatedly(::testing::Return(true));
    EXPECT_CALL(methods, QueryInterceptionHookPoint(::testing::Ne(
                                 grpc::experimental::InterceptionHookPoints::PRE_SEND_MESSAGE)))
            .WillRepeatedly(::testing::Return(false));
    EXPECT_CALL(methods, Proceed());

    // Use EmulatorStatus and fill it with garbage to exceed 200 bytes.
    android::emulation::control::EmulatorStatus large_status;
    large_status.set_version(std::string(300, 'A'));
    EXPECT_CALL(methods, GetSendMessage()).WillOnce(::testing::Return(&large_status));

    interceptor->Intercept(&methods);

    auto crumbs = GetAllCrumbs();

    bool found_large = false;
    for (const auto& c : crumbs) {
        if (c.phase() == GrpcBreadcrumb::PRE_SEND_MESSAGE && c.msg_size() > 200) {
            EXPECT_FALSE(c.has_payload());  // Payload should be dropped as it's too big
            found_large = true;
        }
    }
    EXPECT_TRUE(found_large);
}

TEST_F(BreadcrumbInterceptorTest, CapturesIncomingMessages) {
    auto factory = std::make_unique<BreadcrumbInterceptorFactory>();
    auto interceptor = std::unique_ptr<grpc::experimental::Interceptor>(
            factory->CreateServerInterceptor(nullptr));

    MockInterceptorBatchMethods methods;
    EXPECT_CALL(methods, QueryInterceptionHookPoint(
                                 grpc::experimental::InterceptionHookPoints::POST_RECV_MESSAGE))
            .WillRepeatedly(::testing::Return(true));
    EXPECT_CALL(methods, QueryInterceptionHookPoint(::testing::Ne(
                                 grpc::experimental::InterceptionHookPoints::POST_RECV_MESSAGE)))
            .WillRepeatedly(::testing::Return(false));
    EXPECT_CALL(methods, Proceed());

    android::emulation::control::KeyboardEvent key_event;
    key_event.set_key("A");
    EXPECT_CALL(methods, GetRecvMessage()).WillOnce(::testing::Return(&key_event));

    interceptor->Intercept(&methods);

    auto crumbs = GetAllCrumbs();

    bool found_incoming = false;
    for (const auto& c : crumbs) {
        if (c.phase() == GrpcBreadcrumb::POST_RECV_MESSAGE) {
            EXPECT_TRUE(c.has_payload());
            android::emulation::control::KeyboardEvent captured;
            captured.ParseFromString(c.payload());
            EXPECT_EQ(captured.key(), "A");
            found_incoming = true;
        }
    }
    EXPECT_TRUE(found_incoming);
}

TEST_F(BreadcrumbInterceptorTest, VerifiedCircularBufferWrap) {
    auto factory = std::make_unique<BreadcrumbInterceptorFactory>();

    // 8KB buffer, each message is ~20-30 bytes.
    // Logging 1000 times should definitely wrap.
    for (int i = 0; i < 1000; ++i) {
        auto interceptor = std::unique_ptr<Interceptor>(factory->CreateClientInterceptor(nullptr));
    }

    auto crumbs = GetAllCrumbs();

    // We expect some valid messages at the end of the buffer.
    ASSERT_GT(crumbs.size(), 10);
    EXPECT_EQ(crumbs.back().phase(), GrpcBreadcrumb::END_OF_CALL);
}

TEST_F(BreadcrumbInterceptorTest, CapturesCorrectThreadId) {
    auto factory = std::make_unique<BreadcrumbInterceptorFactory>();
    uint64_t expected_id = android::crashreport::GetOsThreadId();

    {
        auto interceptor = std::unique_ptr<grpc::experimental::Interceptor>(
                factory->CreateClientInterceptor(nullptr));
    }

    auto crumbs = GetAllCrumbs();
    ASSERT_GE(crumbs.size(), 2);

    // Check that the thread_id matches our current thread for both events.
    EXPECT_EQ(crumbs[crumbs.size() - 2].thread_id(), expected_id);
    EXPECT_EQ(crumbs.back().thread_id(), expected_id);
}

}  // namespace android::control::interceptor
