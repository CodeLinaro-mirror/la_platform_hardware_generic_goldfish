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

#include "android/control/interceptor/perfetto_interceptor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "android/base/testing/TestTempDir.h"
#include "goldfish/perfetto/perfetto.h"
#include "goldfish/perfetto/perfetto_categories.h"

namespace android::control::interceptor {
namespace {

using namespace testing;
using namespace grpc::experimental;

bool TraceContainsEvent(const std::filesystem::path& trace_file, const std::string& event_name) {
    std::ifstream input(trace_file, std::ios::binary);
    if (!input.is_open()) return false;

    std::vector<char> buffer((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());

    std::string data(buffer.begin(), buffer.end());
    return data.find(event_name) != std::string::npos;
}

class MockInterceptorBatchMethods : public InterceptorBatchMethods {
  public:
    MOCK_METHOD(bool, QueryInterceptionHookPoint, (InterceptionHookPoints), (override));
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

class PerfettoInterceptorTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() { goldfish::perfetto::Initialize(); }
};

TEST_F(PerfettoInterceptorTest, EmitsTraceEvents) {
    android::base::TestTempDir tmp_dir("perfetto_interceptor_test");
    ASSERT_FALSE(tmp_dir.Path().empty());
    std::filesystem::path trace_file = tmp_dir.MakeSubPath("grpc_trace.pftrace");

    {
        goldfish::perfetto::EmulatorTracingSession session(trace_file, "grpc");

        auto factory = std::make_unique<PerfettoInterceptorFactory>();
        auto interceptor = std::unique_ptr<Interceptor>(factory->CreateServerInterceptor(nullptr));

        MockInterceptorBatchMethods methods;
        EXPECT_CALL(methods, Proceed()).Times(1);
        EXPECT_CALL(methods, QueryInterceptionHookPoint(_)).WillRepeatedly(Return(true));

        interceptor->Intercept(&methods);
    }

    EXPECT_TRUE(std::filesystem::exists(trace_file));
    // We expect the trace to contain a specific event name emitted by the interceptor.
    EXPECT_TRUE(TraceContainsEvent(trace_file, "gRPC Intercept"));
}

}  // namespace
}  // namespace android::control::interceptor
