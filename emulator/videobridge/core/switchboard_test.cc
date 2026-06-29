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
#include <string>
#include <thread>

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

    auto start = std::chrono::steady_clock::now();
    auto maybe_msg = board.NextMessage("user3", absl::Milliseconds(500));
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();

    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg), "hello");
    // Should have returned early rather than waiting full 500ms
    EXPECT_LT(duration, 200);

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

TEST(SwitchboardTest, NextMessageCallbackImmediate) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Send a message first
    board.Send("user", "hello");

    // Call NextMessage with a callback, it should be invoked immediately
    std::string received;
    bool invoked = false;
    board.NextMessage("user", [&](absl::StatusOr<std::string> msg) {
        ASSERT_TRUE(msg.ok());
        received = std::move(*msg);
        invoked = true;
    });

    EXPECT_TRUE(invoked);
    EXPECT_EQ(nlohmann::json::parse(received), "hello");

    board.Disconnect("user");
}

TEST(SwitchboardTest, NextMessageCallbackDelayed) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Register callback on empty queue
    std::string received;
    bool invoked = false;
    board.NextMessage("user", [&](absl::StatusOr<std::string> msg) {
        ASSERT_TRUE(msg.ok());
        received = std::move(*msg);
        invoked = true;
    });

    EXPECT_FALSE(invoked);

    // Send a message
    board.Send("user", "world");

    // Callback should have been invoked
    EXPECT_TRUE(invoked);
    EXPECT_EQ(nlohmann::json::parse(received), "world");

    board.Disconnect("user");
}

TEST(SwitchboardTest, NextMessageCallbackDisconnect) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    // Register callback on empty queue
    absl::Status status = absl::OkStatus();
    bool invoked = false;
    board.NextMessage("user", [&](absl::StatusOr<std::string> msg) {
        status = msg.status();
        invoked = true;
    });

    EXPECT_FALSE(invoked);

    // Disconnect user (destroys the queue and triggers callback with cancelled status)
    board.Disconnect("user");

    EXPECT_TRUE(invoked);
    EXPECT_EQ(status.code(), absl::StatusCode::kCancelled);
}

TEST(SwitchboardTest, NextMessageBlockedInterruptedByDisconnect) {
    Switchboard board(nullptr);
    EXPECT_TRUE(board.Connect("user", "{}"));

    absl::Status status = absl::OkStatus();
    std::thread t([&board, &status]() {
        // This will block until disconnected or timed out. We set a large timeout
        // to make sure it gets interrupted by the Disconnect call instead.
        auto maybe_msg = board.NextMessage("user", absl::Seconds(10));
        status = maybe_msg.status();
    });

    // Give the thread a moment to enter NextMessage and block.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Disconnect should unblock the thread (which will eventually time out since no message
    // arrives).
    board.Disconnect("user");
    t.join();

    EXPECT_EQ(status.code(), absl::StatusCode::kDeadlineExceeded);
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

}  // namespace
}  // namespace goldfish::videobridge
