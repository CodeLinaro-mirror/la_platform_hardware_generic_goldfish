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

#include "goldfish/videobridge/switchboard.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "nlohmann/json.hpp"

namespace goldfish::videobridge {
namespace {

TEST(SwitchboardTest, ConnectDisconnectParticipant) {
    Switchboard board(nullptr);

    // Initial state: not connected (should return deadline exceeded / not found)
    EXPECT_FALSE(board.NextMessage("user1", absl::Milliseconds(10)).ok());

    // Connect user1
    EXPECT_TRUE(board.Connect("user1", "{}"));

    // Send a message to user1
    board.Send("user1", {{"type", "offer"}, {"sdp", "v=0..."}});

    // Pull from queue
    auto maybe_msg = board.NextMessage("user1", absl::Milliseconds(100));
    ASSERT_TRUE(maybe_msg.ok());
    auto parsed = nlohmann::json::parse(*maybe_msg);
    EXPECT_EQ(parsed["type"], "offer");

    // Disconnect user1
    board.Disconnect("user1");

    // Queue is gone
    EXPECT_FALSE(board.NextMessage("user1", absl::Milliseconds(10)).ok());
}

TEST(SwitchboardTest, NextMessageBlocksAndTimesOut) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user2"));

    auto start = std::chrono::steady_clock::now();
    auto maybe_msg = board.NextMessage("user2", absl::Milliseconds(150));
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();

    EXPECT_FALSE(maybe_msg.ok());
    EXPECT_EQ(maybe_msg.status().code(), absl::StatusCode::kDeadlineExceeded);
    // Timeout should be respected (roughly 150ms)
    EXPECT_GE(duration, 140);
}

TEST(SwitchboardTest, NextMessageBlocksAndReceives) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user3"));

    std::thread t([&board]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        board.Send("user3", "hello");
    });

    auto maybe_msg = board.NextMessage("user3", absl::Seconds(5));

    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "hello");

    t.join();
}

TEST(SwitchboardTest, ConnectAlreadyConnected) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));
    // Connecting again should return true (already connected, harmless)
    EXPECT_TRUE(board.Connect("user", "{}"));
    board.Disconnect("user");
}

TEST(SwitchboardTest, DisconnectUnconnected) {
    Switchboard board(nullptr);
    // Disconnecting a non-existent participant should not crash and return safely
    board.Disconnect("non-existent");
}

TEST(SwitchboardTest, AcceptJsepMessagesValidation) {
    Switchboard board(nullptr);

    // Send message to unconnected participant -> expect failure (NotFoundError)
    EXPECT_EQ(board.AcceptJsepMessage("user", "{}").code(), absl::StatusCode::kNotFound);

    // Connect participant
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Send malformed JSON -> expect failure (InvalidArgumentError)
    EXPECT_EQ(board.AcceptJsepMessage("user", "invalid json").code(),
              absl::StatusCode::kInvalidArgument);

    // Send valid JSON but not containing a valid JSEP payload (e.g. empty dictionary)
    // Any valid JSON parsed successfully will return Ok from AcceptJsepMessage
    EXPECT_TRUE(board.AcceptJsepMessage("user", "{}").ok());

    board.Disconnect("user");
}

// A signaling client can put anything on the wire, and AcceptJsepMessage is
// the door it comes through. Routing runs as a BlockingCall onto the WebRTC
// signaling thread, so a parser that throws instead of returning a status
// unwinds into Thread::Dispatch and terminates the emulator. That makes every
// payload below a remotely triggerable abort; the assertion that matters here
// is simply that the test process is still alive at the end.
TEST(SwitchboardTest, SurvivesHostileJsepPayloads) {
    Switchboard board(nullptr);
    ASSERT_TRUE(board.Connect("user", "{}"));

    constexpr const char* kHostilePayloads[] = {
        // Fields of the wrong JSON type. Present, so a contains() check
        // passes them through to a conversion that has no reason to succeed.
        R"({"type": 42, "sdp": "v=0"})",
        R"({"type": "offer", "sdp": 42})",
        R"({"type": null, "sdp": null})",
        R"({"type": ["offer"], "sdp": {"a": 1}})",
        // The same, wrapped in the nested envelope the parsers also accept.
        R"({"sdp": {"type": 42, "sdp": "v=0"}})",
        // Values that are not objects where an object is expected.
        R"({"sdp": true})",
        R"({"sdp": [1, 2, 3]})",
        // The candidate arm of the dispatch.
        R"({"candidate": {"sdpMid": 0, "sdpMLineIndex": 0, "candidate": "c"}})",
        R"({"candidate": {"sdpMid": "0", "sdpMLineIndex": "0", "candidate": "c"}})",
        R"({"candidate": 42})",
        // Both arms in one message, so neither can mask the other.
        R"({"candidate": null, "sdp": null})",
    };

    for (const char* payload : kHostilePayloads) {
        EXPECT_TRUE(board.AcceptJsepMessage("user", payload).ok())
                << "Payload was rejected at the transport layer rather than by the "
                   "parser, so it never reached the code under test: "
                << payload;
    }

    // Still serving after all of that.
    EXPECT_TRUE(board.AcceptJsepMessage("user", R"({"sdp": {"type": "offer"}})").ok());
    board.Disconnect("user");
}

TEST(SwitchboardTest, NextMessageFIFOOrdering) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    board.Send("user", "first");
    board.Send("user", "second");
    board.Send("user", "third");

    auto maybe_msg = board.NextMessage("user", absl::Milliseconds(10));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "first");

    maybe_msg = board.NextMessage("user", absl::Milliseconds(10));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "second");

    maybe_msg = board.NextMessage("user", absl::Milliseconds(10));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "third");

    // No more messages
    EXPECT_FALSE(board.NextMessage("user", absl::Milliseconds(10)).ok());

    board.Disconnect("user");
}

TEST(SwitchboardTest, MultipleParticipantsNoCrosstalk) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("userA", "{}"));
    EXPECT_TRUE(board.Connect("userB", "{}"));

    board.Send("userA", "msgA");
    board.Send("userB", "msgB");

    auto maybe_msg = board.NextMessage("userA", absl::Milliseconds(10));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "msgA");

    maybe_msg = board.NextMessage("userB", absl::Milliseconds(10));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "msgB");

    board.Disconnect("userA");
    board.Disconnect("userB");
}

TEST(SwitchboardTest, DisconnectCleansUpParticipant) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Send JSEP is valid when connected
    EXPECT_TRUE(board.AcceptJsepMessage("user", "{}").ok());

    // Disconnect participant
    board.Disconnect("user");

    // Verify participant is removed and no longer accepts signaling messages
    EXPECT_EQ(board.AcceptJsepMessage("user", "{}").code(), absl::StatusCode::kNotFound);
}

TEST(SwitchboardTest, DisconnectCleansUpQueue) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Send a message
    board.Send("user", "hello");

    // Disconnect user
    board.Disconnect("user");

    // Verify that the queue is cleared/removed, and we cannot retrieve the message
    EXPECT_FALSE(board.NextMessage("user", absl::Milliseconds(10)).ok());
}

TEST(SwitchboardTest, NextMessageBlockedInterruptedByDisconnect) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    absl::Status status = absl::OkStatus();
    const absl::Time start = absl::Now();
    std::thread t([&board, &status]() {
        // Long timeout ensures Disconnect interrupts the wait, not the deadline.
        auto maybe_msg = board.NextMessage("user", absl::Seconds(10));
        status = maybe_msg.status();
    });

    // Sticky closed flag unblocks readers even if Disconnect runs first.
    board.Disconnect("user");
    t.join();
    const absl::Duration elapsed = absl::Now() - start;

    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(absl::IsDeadlineExceeded(status)) << "Reader waited out its deadline: " << status;
    EXPECT_LT(elapsed, absl::Seconds(5)) << "Disconnect did not promptly wake the blocked reader.";
}

TEST(SwitchboardTest, NextMessageZeroTimeout) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Immediately returns deadline exceeded on empty queue
    auto maybe_msg = board.NextMessage("user", absl::ZeroDuration());
    EXPECT_FALSE(maybe_msg.ok());
    EXPECT_EQ(maybe_msg.status().code(), absl::StatusCode::kDeadlineExceeded);

    // If a message is already in queue, it should still return it even with zero timeout
    board.Send("user", "instant");
    maybe_msg = board.NextMessage("user", absl::ZeroDuration());
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "instant");

    board.Disconnect("user");
}

TEST(SwitchboardTest, ManyConcurrentParticipants) {
    Switchboard board(nullptr);
    const int kNumParticipants = 10;
    std::vector<std::string> identities;

    for (int i = 0; i < kNumParticipants; ++i) {
        identities.push_back(absl::StrCat("user_", i));
        EXPECT_TRUE(board.Connect(identities.back(), "{}"));
    }

    // Send messages to all
    for (int i = 0; i < kNumParticipants; ++i) {
        board.Send(identities[i], absl::StrCat("msg_", i));
    }

    // Read and verify without crosstalk
    for (int i = 0; i < kNumParticipants; ++i) {
        auto maybe_msg = board.NextMessage(identities[i], absl::Milliseconds(50));
        ASSERT_TRUE(maybe_msg.ok()) << "Failed to read for " << identities[i];
        EXPECT_EQ(nlohmann::json::parse(*maybe_msg), absl::StrCat("msg_", i));
    }

    for (const auto& id : identities) {
        board.Disconnect(id);
    }
}

class MockInputSender : public InputSender {
  public:
    absl::Status Start() override { return absl::OkStatus(); }
    void SendEvent(const InputEvent& /*event*/) override {}
    void Stop() override {}
};

TEST(SwitchboardTest, CustomInputSenderFactory) {
    bool factory_called = false;
    auto factory = [&factory_called](DataChannelLabel label) -> std::unique_ptr<InputSender> {
        factory_called = true;
        EXPECT_EQ(label, DataChannelLabel::kInput);
        return std::make_unique<MockInputSender>();
    };

    Switchboard board(nullptr, factory);
    auto sender = board.CreateInputSender(DataChannelLabel::kInput);
    EXPECT_TRUE(factory_called);
    EXPECT_NE(sender, nullptr);
}

}  // namespace
}  // namespace goldfish::videobridge
