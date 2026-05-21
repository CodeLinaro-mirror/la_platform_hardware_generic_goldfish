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
#include "android/emulation/control/simple_async_grpc.h"

#include <gtest/gtest.h>

#include <functional>

namespace android::emulation::control {

template <typename R>
class FakeReactor {
  public:
    virtual ~FakeReactor() = default;
    virtual void OnReadDone(bool ok) = 0;
    virtual void OnDone() {}

    void StartRead(R* msg) {
        start_read_called = true;
        last_read_msg = msg;
    }

    void Finish(grpc::Status status) {
        finish_called = true;
        finish_status = status;
    }

    bool start_read_called = false;
    bool finish_called = false;
    grpc::Status finish_status = grpc::Status::OK;
    R* last_read_msg = nullptr;
};

TEST(SimpleAsyncGrpcTest, ReadErrorDoesNotCallStartRead) {
    struct MyMessage {
        int value;
    };

    bool read_called = false;
    SimpleServerLambdaReader<MyMessage, FakeReactor<MyMessage>> reader([&](const MyMessage* msg) {
        read_called = true;
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "test error");
    });

    // Simulate gRPC calling OnReadDone with ok=true
    reader.OnReadDone(true);

    EXPECT_TRUE(read_called);
    EXPECT_TRUE(reader.finish_called);
    EXPECT_EQ(reader.finish_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    // This should be false, if it is true, we will crash.
    EXPECT_FALSE(reader.start_read_called);
}

TEST(SimpleAsyncGrpcTest, ReadSuccessCallsStartRead) {
    struct MyMessage {
        int value;
    };

    bool read_called = false;
    SimpleServerLambdaReader<MyMessage, FakeReactor<MyMessage>> reader([&](const MyMessage* msg) {
        read_called = true;
        return grpc::Status::OK;
    });

    // Simulate gRPC calling OnReadDone with ok=true
    reader.OnReadDone(true);

    EXPECT_TRUE(read_called);
    EXPECT_FALSE(reader.finish_called);
    EXPECT_TRUE(reader.start_read_called);
}

}  // namespace android::emulation::control
