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
#include "goldfish/adb/adb_breadcrumb_tracker.h"

#include <gtest/gtest.h>

#include "android/crashreport/binary_annotation.h"
#include "goldfish/circular_message_log.h"

namespace goldfish::adb {

using android::control::breadcrumbs::Breadcrumb;
using android::crashreport::BinaryAnnotation;
using goldfish::proto_data_store::ProtoCircularLog;

TEST(AdbBreadcrumbTrackerTest, LogsPackets) {
    AdbBreadcrumbTracker tracker(true);
    APacket packet;
    packet.message.command = kAdbCnxn;
    packet.message.arg0 = 1;
    packet.message.arg1 = 2;
    packet.message.data_length = 0;
    packet.message.magic = packet.message.command ^ 0xffffffff;

    tracker.OnPacket(packet.message, packet.data, true);  // toGuest = true

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    uint32_t expected_command = packet.message.command;
    uint64_t expected_flow_id =
            (static_cast<uint64_t>(packet.message.arg0) << 32) | packet.message.arg1;
    bool found = false;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == expected_command) {
            EXPECT_EQ(event.flow_id(), expected_flow_id);
            EXPECT_EQ(event.phase(), Breadcrumb::INSTANT);
            EXPECT_EQ(event.adb().direction(), android::control::breadcrumbs::AdbPayload::TO_GUEST);
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, SyncStreamTruncation) {
    AdbBreadcrumbTracker tracker(true);

    // 1. Open a sync stream (A -> B)
    AMessage open_msg;
    open_msg.command = kAdbOpen;
    open_msg.arg0 = 1000;  // local-id A
    open_msg.arg1 = 0;     // remote-id is 0 for OPEN
    open_msg.data_length = 5;
    tracker.OnPacket(open_msg, "sync:", true);

    // 2. Send OKAY from responder (B -> A)
    AMessage okay_msg;
    okay_msg.command = kAdbOkay;
    okay_msg.arg0 = 2000;  // local-id B
    okay_msg.arg1 = 1000;  // remote-id A
    okay_msg.data_length = 0;
    tracker.OnPacket(okay_msg, nullptr, false);

    // 3. Send data on the sync stream (A -> B)
    AMessage wrte_msg;
    wrte_msg.command = kAdbWrte;
    wrte_msg.arg0 = 1000;
    wrte_msg.arg1 = 2000;
    wrte_msg.data_length = 8;
    const char* wrte_data = "STAT1234";
    tracker.OnPacket(wrte_msg, wrte_data, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(1000) << 32) | 2000;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            EXPECT_EQ(event.adb().data_snippet().size(), 4);
            EXPECT_EQ(event.adb().data_snippet(), "STAT");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, EvictsOldestStream) {
    AdbBreadcrumbTracker tracker(true);

    // 1. Open 31 sync streams
    for (int i = 0; i < 31; ++i) {
        AMessage open_msg;
        open_msg.command = kAdbOpen;
        open_msg.arg0 = i;
        open_msg.arg1 = 0;
        open_msg.data_length = 5;
        tracker.OnPacket(open_msg, "sync:", true);

        AMessage okay_msg;
        okay_msg.command = kAdbOkay;
        okay_msg.arg0 = 100 + i;
        okay_msg.arg1 = i;
        okay_msg.data_length = 0;
        tracker.OnPacket(okay_msg, nullptr, false);
    }

    // 2. Open the 32nd stream (should evict stream 0)
    AMessage open_msg_32;
    open_msg_32.command = kAdbOpen;
    open_msg_32.arg0 = 31;
    open_msg_32.arg1 = 0;
    open_msg_32.data_length = 5;
    tracker.OnPacket(open_msg_32, "sync:", true);

    AMessage okay_msg_32;
    okay_msg_32.command = kAdbOkay;
    okay_msg_32.arg0 = 131;
    okay_msg_32.arg1 = 31;
    okay_msg_32.data_length = 0;
    tracker.OnPacket(okay_msg_32, nullptr, false);

    // 3. Send data on the 1st stream (flow_id derived from arg0=0, arg1=100 -> 100)
    AMessage wrte_msg;
    wrte_msg.command = kAdbWrte;
    wrte_msg.arg0 = 0;
    wrte_msg.arg1 = 100;
    wrte_msg.data_length = 8;
    const char* wrte_data = "STAT1234";

    tracker.OnPacket(wrte_msg, wrte_data, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = 100;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            // Since it was evicted, it should NOT be treated as a sync stream,
            // so it should capture the full snippet (up to 32 bytes, here 8 bytes).
            EXPECT_EQ(event.adb().data_snippet().size(), 8);
            EXPECT_EQ(event.adb().data_snippet(), "STAT1234");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, PayloadTruncation) {
    AdbBreadcrumbTracker tracker(true);

    // 1. Open a normal stream
    AMessage open_msg;
    open_msg.command = kAdbOpen;
    open_msg.arg0 = 3000;
    open_msg.arg1 = 0;
    open_msg.data_length = 6;
    tracker.OnPacket(open_msg, "shell:", true);

    AMessage okay_msg;
    okay_msg.command = kAdbOkay;
    okay_msg.arg0 = 4000;
    okay_msg.arg1 = 3000;
    okay_msg.data_length = 0;
    tracker.OnPacket(okay_msg, nullptr, false);

    // 2. Send large data on the stream
    AMessage wrte_msg;
    wrte_msg.command = kAdbWrte;
    wrte_msg.arg0 = 3000;
    wrte_msg.arg1 = 4000;
    wrte_msg.data_length = 64;

    std::string large_data(64, 'A');
    tracker.OnPacket(wrte_msg, large_data.c_str(), true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(3000) << 32) | 4000;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            EXPECT_EQ(event.adb().data_snippet().size(), 32);  // Capped at kMaxPayloadSnippetSize
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, Directionality) {
    AdbBreadcrumbTracker tracker(true);

    AMessage msg;
    msg.command = kAdbCnxn;
    msg.arg0 = 0;
    msg.arg1 = 0;
    msg.data_length = 0;

    // Test TO_HOST
    tracker.OnPacket(msg, nullptr, false);  // toGuest = false

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() &&
            event.adb().direction() == android::control::breadcrumbs::AdbPayload::TO_HOST) {
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, StreamClosure) {
    AdbBreadcrumbTracker tracker(true);

    // 1. Open a sync stream
    AMessage open_msg;
    open_msg.command = kAdbOpen;
    open_msg.arg0 = 5000;
    open_msg.arg1 = 0;
    open_msg.data_length = 5;
    tracker.OnPacket(open_msg, "sync:", true);

    AMessage okay_msg;
    okay_msg.command = kAdbOkay;
    okay_msg.arg0 = 6000;
    okay_msg.arg1 = 5000;
    okay_msg.data_length = 0;
    tracker.OnPacket(okay_msg, nullptr, false);

    // 2. Close the stream
    AMessage clse_msg;
    clse_msg.command = kAdbClse;
    clse_msg.arg0 = 5000;
    clse_msg.arg1 = 6000;
    clse_msg.data_length = 0;
    tracker.OnPacket(clse_msg, nullptr, true);

    // 3. Send data on the closed stream
    AMessage wrte_msg;
    wrte_msg.command = kAdbWrte;
    wrte_msg.arg0 = 5000;
    wrte_msg.arg1 = 6000;
    wrte_msg.data_length = 8;
    const char* wrte_data = "STAT1234";

    tracker.OnPacket(wrte_msg, wrte_data, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(5000) << 32) | 6000;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            // Since stream was closed, it should NOT be treated as sync stream,
            // so it should capture full snippet (8 bytes).
            EXPECT_EQ(event.adb().data_snippet().size(), 8);
            EXPECT_EQ(event.adb().data_snippet(), "STAT1234");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, NullDataHandling) {
    AdbBreadcrumbTracker tracker(true);

    AMessage msg;
    msg.command = kAdbCnxn;
    msg.arg0 = 7000;
    msg.arg1 = 8000;
    msg.data_length = 10;  // Claims to have data

    // Pass nullptr for data
    tracker.OnPacket(msg, nullptr, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(7000) << 32) | 8000;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbCnxn &&
            event.flow_id() == expected_flow_id) {
            EXPECT_TRUE(event.adb().data_snippet().empty());
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, DirectionalIdCollisions) {
    AdbBreadcrumbTracker tracker(true);

    AMessage open_host;
    open_host.command = kAdbOpen;
    open_host.arg0 = 5;
    open_host.arg1 = 0;
    open_host.data_length = 5;
    tracker.OnPacket(open_host, "sync:", true);

    AMessage open_guest;
    open_guest.command = kAdbOpen;
    open_guest.arg0 = 5;
    open_guest.arg1 = 0;
    open_guest.data_length = 6;
    tracker.OnPacket(open_guest, "shell:", false);

    AMessage okay_guest;
    okay_guest.command = kAdbOkay;
    okay_guest.arg0 = 100;
    okay_guest.arg1 = 5;
    okay_guest.data_length = 0;
    tracker.OnPacket(okay_guest, nullptr, false);

    AMessage wrte_host;
    wrte_host.command = kAdbWrte;
    wrte_host.arg0 = 5;
    wrte_host.arg1 = 100;
    wrte_host.data_length = 8;
    const char* wrte_data = "STAT1234";
    tracker.OnPacket(wrte_host, wrte_data, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(5) << 32) | 100;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            EXPECT_EQ(event.adb().data_snippet().size(), 4);
            EXPECT_EQ(event.adb().data_snippet(), "STAT");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, ClseRejectionCleansUpPending) {
    AdbBreadcrumbTracker tracker(true);

    AMessage open_msg;
    open_msg.command = kAdbOpen;
    open_msg.arg0 = 7;
    open_msg.arg1 = 0;
    open_msg.data_length = 5;
    tracker.OnPacket(open_msg, "sync:", true);

    AMessage clse_msg;
    clse_msg.command = kAdbClse;
    clse_msg.arg0 = 0;
    clse_msg.arg1 = 7;
    clse_msg.data_length = 0;
    tracker.OnPacket(clse_msg, nullptr, false);

    AMessage okay_msg;
    okay_msg.command = kAdbOkay;
    okay_msg.arg0 = 700;
    okay_msg.arg1 = 7;
    okay_msg.data_length = 0;
    tracker.OnPacket(okay_msg, nullptr, false);

    AMessage wrte_msg;
    wrte_msg.command = kAdbWrte;
    wrte_msg.arg0 = 7;
    wrte_msg.arg1 = 700;
    wrte_msg.data_length = 8;
    const char* wrte_data = "STAT1234";
    tracker.OnPacket(wrte_msg, wrte_data, true);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(7) << 32) | 700;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            EXPECT_EQ(event.adb().data_snippet().size(), 8);
            EXPECT_EQ(event.adb().data_snippet(), "STAT1234");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

TEST(AdbBreadcrumbTrackerTest, LogsOutOfSyncEvents) {
    AdbBreadcrumbTracker tracker(true);

    tracker.OnOutOfSync("Test reason guest", true);
    tracker.OnOutOfSync("Test reason host", false);

    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found_guest = false;
    bool found_host = false;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == 0) {
            if (event.adb().data_snippet() == "Test reason guest") {
                EXPECT_EQ(event.phase(), Breadcrumb::INSTANT);
                EXPECT_EQ(event.adb().direction(),
                          android::control::breadcrumbs::AdbPayload::TO_GUEST);
                found_guest = true;
            } else if (event.adb().data_snippet() == "Test reason host") {
                EXPECT_EQ(event.phase(), Breadcrumb::INSTANT);
                EXPECT_EQ(event.adb().direction(),
                          android::control::breadcrumbs::AdbPayload::TO_HOST);
                found_host = true;
            }
        }
        return true;
    });

    EXPECT_TRUE(found_guest);
    EXPECT_TRUE(found_host);
}

TEST(AdbBreadcrumbTrackerTest, InvariantMismatchedEvictionTest) {
    AdbBreadcrumbTracker tracker(true);

    // 1. Open a sync stream with ID 1 twice (duplicate)
    AMessage open_msg_1;
    open_msg_1.command = kAdbOpen;
    open_msg_1.arg0 = 1;
    open_msg_1.arg1 = 0;
    open_msg_1.data_length = 5;
    tracker.OnPacket(open_msg_1, "sync:", true);
    tracker.OnPacket(open_msg_1, "sync:", true);

    // 2. Close stream 1 (erases it from pending_opens_ but leaves one in pending_opens_order_ if
    // bug is present)
    AMessage clse_msg_1;
    clse_msg_1.command = kAdbClse;
    clse_msg_1.arg0 = 1;
    clse_msg_1.arg1 = 0;
    clse_msg_1.data_length = 0;
    tracker.OnPacket(clse_msg_1, nullptr, true);

    // 3. Open 16 other unique sync streams (IDs 101 to 116)
    for (int i = 101; i <= 116; ++i) {
        AMessage open_msg;
        open_msg.command = kAdbOpen;
        open_msg.arg0 = i;
        open_msg.arg1 = 0;
        open_msg.data_length = 5;
        tracker.OnPacket(open_msg, "sync:", true);
    }

    // 4. Open 17th stream (ID 117).
    // If the bug is fixed, the queue had 16 items (101..116). Opening 117 evicts the oldest (101).
    // If the bug is present, the queue had 17 items (stale 1, 101..116). Opening 117 evicts
    // stale 1. 101 is NOT evicted.
    AMessage open_msg_117;
    open_msg_117.command = kAdbOpen;
    open_msg_117.arg0 = 117;
    open_msg_117.arg1 = 0;
    open_msg_117.data_length = 5;
    tracker.OnPacket(open_msg_117, "sync:", true);

    // 5. Send OKAY for stream 101.
    AMessage okay_msg_101;
    okay_msg_101.command = kAdbOkay;
    okay_msg_101.arg0 = 9101;  // responder ID
    okay_msg_101.arg1 = 101;   // initiator ID
    okay_msg_101.data_length = 0;
    tracker.OnPacket(okay_msg_101, nullptr, false);

    // 6. Write to stream 101.
    AMessage wrte_msg_101;
    wrte_msg_101.command = kAdbWrte;
    wrte_msg_101.arg0 = 101;
    wrte_msg_101.arg1 = 9101;
    wrte_msg_101.data_length = 8;
    tracker.OnPacket(wrte_msg_101, "STAT1234", true);

    // 7. Verify. If the bug is fixed, 101 was evicted, so it is NOT treated as a sync stream.
    // Thus, it will capture the full snippet (8 bytes).
    // If the bug is present, 101 was NOT evicted, so it is treated as a sync stream and truncated
    // to 4 bytes.
    auto* log = AdbBreadcrumbTracker::GetLogForTesting();
    ASSERT_NE(log, nullptr);

    bool found = false;
    uint64_t expected_flow_id = (static_cast<uint64_t>(101) << 32) | 9101;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& event = static_cast<const Breadcrumb&>(msg);
        if (event.has_adb() && event.adb().command() == kAdbWrte &&
            event.flow_id() == expected_flow_id) {
            EXPECT_EQ(event.adb().data_snippet().size(), 8);
            EXPECT_EQ(event.adb().data_snippet(), "STAT1234");
            found = true;
        }
        return true;
    });

    EXPECT_TRUE(found);
}

}  // namespace goldfish::adb
