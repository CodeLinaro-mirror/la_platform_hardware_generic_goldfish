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
#include "emulator/plugin/grpc/input/dispatcher_session.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "absl/status/status.h"

#include "emulator/plugin/grpc/input/input_dispatcher_interface.h"
#include "emulator/plugin/grpc/input/recording_input_dispatcher.h"
#include "input_service.pb.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::InputEvent;
using ::goldfish::grpc::v2::testing::RecordingInputDispatcher;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

class DispatcherSessionTest : public ::testing::Test {
  protected:
    // Dispatches `event` on a new session and returns the status it completed
    // with on the calling thread, if it did.
    std::optional<absl::Status> DispatchAndWait(const InputEvent& event) {
        DispatcherSession session(dispatcher_, {.human = true, .source_id = 0});
        std::optional<absl::Status> result;
        session.Dispatch(event, [&result, caller = std::this_thread::get_id()](absl::Status s) {
            EXPECT_EQ(std::this_thread::get_id(), caller);
            result = std::move(s);
        });
        return result;
    }

    std::shared_ptr<RecordingInputDispatcher> dispatcher_ =
            std::make_shared<RecordingInputDispatcher>();
};

TEST_F(DispatcherSessionTest, OpensWithOptionsAndClosesOnDestruction) {
    {
        DispatcherSession session(dispatcher_, {.human = true, .source_id = 42});
        ASSERT_TRUE(dispatcher_->opened_with.has_value());
        EXPECT_TRUE(dispatcher_->opened_with->human);
        EXPECT_EQ(dispatcher_->opened_with->source_id, 42);
        EXPECT_THAT(dispatcher_->calls, IsEmpty());
    }
    EXPECT_THAT(dispatcher_->calls, ElementsAre("close 7"));
}

TEST_F(DispatcherSessionTest, RoutesPointerKeyAndTextWithItsSessionId) {
    {
        DispatcherSession session(dispatcher_, {});
        InputEvent pointer;
        pointer.mutable_pointer()->set_display_id(2);
        InputEvent key;
        key.mutable_key()->set_dom_code("KeyA");
        InputEvent text;
        text.mutable_text()->set_text("hi 测试_🚀_é");

        session.Dispatch(pointer);
        session.Dispatch(key);
        session.Dispatch(text);
    }

    EXPECT_THAT(dispatcher_->calls,
                ElementsAre("pointer 7 display 2", "key 7 KeyA", "text 7 hi 测试_🚀_é", "close 7"));
}

TEST_F(DispatcherSessionTest, ForwardsTheCompletion) {
    InputEvent pointer;
    pointer.mutable_pointer();

    EXPECT_EQ(DispatchAndWait(pointer), absl::OkStatus());
}

TEST_F(DispatcherSessionTest, AxisIsUnimplementedWithoutReachingTheDispatcher) {
    InputEvent axis;
    axis.mutable_axis();

    std::optional<absl::Status> result = DispatchAndWait(axis);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->code(), absl::StatusCode::kUnimplemented);
    EXPECT_THAT(dispatcher_->calls, ElementsAre("close 7"));
}

TEST_F(DispatcherSessionTest, UnsetEventIsInvalidArgument) {
    std::optional<absl::Status> result = DispatchAndWait(InputEvent());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(dispatcher_->calls, ElementsAre("close 7"));
}

TEST_F(DispatcherSessionTest, RejectsWithoutACallback) {
    DispatcherSession session(dispatcher_, {});

    session.Dispatch(InputEvent());
    InputEvent axis;
    axis.mutable_axis();
    session.Dispatch(axis);

    EXPECT_THAT(dispatcher_->calls, IsEmpty());
}

}  // namespace
}  // namespace goldfish::grpc::v2
