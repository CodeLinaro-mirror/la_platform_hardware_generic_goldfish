// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "goldfish/adb/adb_message_logger.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace {

using goldfish::adb::AdbMessageLogger;
using goldfish::adb::AdbPacketCallback;

class MockAdbPacketCallback : public AdbPacketCallback {
  public:
    MOCK_METHOD(void, OnPacket,
                (const goldfish::adb::AMessage& message, const char* data, bool to_guest),
                (override));
    MOCK_METHOD(void, OnOutOfSync, (const std::string& reason), (override));
};
using goldfish::adb::AMessage;
using goldfish::adb::APacket;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

using Message = std::string;

enum class AdbWireMessage {
    A_SYNC = 0x434e5953,
    A_CNXN = 0x4e584e43,
    A_AUTH = 0x48545541,
    A_OPEN = 0x4e45504f,
    A_OKAY = 0x59414b4f,
    A_CLSE = 0x45534c43,
    A_WRTE = 0x45545257,
};

// Maximum payload for latest adb version (see system/core/adb/adb.h)
constexpr size_t MAX_ADB_MESSAGE_PAYLOAD = 1024 * 1024;

Message addMessage(AdbWireMessage cmd, uint32_t arg0, uint32_t arg1, std::string msg = "") {
    AMessage message = {(uint32_t)cmd,        arg0, arg1,
                        (uint32_t)msg.size(), 0,    (uint32_t)cmd ^ 0xffffffff};
    auto wire = std::string((char*)&message, sizeof(message));
    if (!msg.empty()) wire += msg;
    return wire;
}

Message connectMsg() {
    return addMessage(AdbWireMessage::A_CNXN, 0x01000001, 1024 * 255, "features=shell_v2,foo,bar");
}

// An invalid message that has a wrong magic value.
Message invalidMagicMsg() {
    AMessage message{0, 1, 2, 3, 4, 5};
    return std::string((char*)&message, sizeof(message));
}

// An invalid message that has wrong length, this will result in issues down the line
Message invalidLengthMsg() {
    return addMessage(AdbWireMessage::A_CNXN, 0, 0, "x") + "xx";
}

Message smallMsg() {
    return addMessage(AdbWireMessage::A_WRTE, 0, 0, "x");
}

Message emptyMsg() {
    return addMessage(AdbWireMessage::A_WRTE, 0, 0, "");
}

Message openMsg(std::string msg) {
    return addMessage(AdbWireMessage::A_OPEN, 1, 1, msg);
}

Message writeMsg(std::string msg) {
    return addMessage(AdbWireMessage::A_WRTE, 1, 1, msg);
}

struct CaptureLogSink : public absl::LogSink {
  public:
    void Send(const absl::LogEntry& entry) override {
        captured_log_ = absl::StrFormat("%s", entry.text_message());
    }

    void clear() { captured_log_.clear(); }

    std::string captured_log_;
};

class AdbMessageLoggerTest : public ::testing::Test {
  protected:
    AdbMessageLoggerTest() {}
    void SetUp() override {
        // Add the CaptureLogSink
        log_sink_ = std::make_unique<CaptureLogSink>();
        absl::AddLogSink(log_sink_.get());
        absl::SetVLogLevel("*", 2);
        logger_ = std::make_unique<AdbMessageLogger>("test> ", true, true);
    }

    void TearDown() override {
        // Remove the CaptureLogSink
        absl::RemoveLogSink(log_sink_.get());
        logger_.reset();
    }

    void sendMessage(Message msg) { logger_->Observe(msg.data(), msg.size()); }

    std::string messageStringOfAtLeast(int bytes) {
        std::string msg = connectMsg();
        msg.reserve(bytes + msg.size());
        while (msg.size() < bytes) {
            msg.append(connectMsg());
            msg.append(smallMsg());
            msg.append(emptyMsg());
        }

        return msg;
    }

    std::unique_ptr<CaptureLogSink> log_sink_;
    std::unique_ptr<AdbMessageLogger> logger_;
};

TEST_F(AdbMessageLoggerTest, logs_a_single_message) {
    sendMessage(openMsg("shell:exit"));
}

TEST_F(AdbMessageLoggerTest, logs_multiple_commands) {
    sendMessage(connectMsg());
    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr("\"command\":\"CNXN"));
    log_sink_->clear();
    const std::vector<std::string> shellMsgs{"shell:exit",
                                             "shell:getprop",
                                             "shell:cat /sys/class/power_supply/*/capacity",
                                             "framebuffer",
                                             "shell_v2,raw:exit",
                                             "shell_v2,raw:getprop",
                                             "shell_v2,raw:cat /sys/class/power_supply/*/capacity"};
    for (auto cmd : shellMsgs) {
        sendMessage(openMsg(cmd));
        EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr(cmd));
        log_sink_->clear();
    }
}

TEST_F(AdbMessageLoggerTest, invalid_length_stops_logging) {
    // Logs a bad message
    sendMessage(invalidLengthMsg());

    // Send a lot of data..
    sendMessage(smallMsg() + std::string(500 * 1024, 'a'));

    EXPECT_THAT(log_sink_->captured_log_,
                testing::HasSubstr("ADB passive logging parser got out of sync and is disabling "
                                   "further ADB logging."));

    log_sink_->clear();

    // No more logging...
    for (int i = 0; i < 100 * 1000; i++) {
        sendMessage(openMsg("shell:exit"));
        EXPECT_EQ(log_sink_->captured_log_, "");
    }
}

TEST_F(AdbMessageLoggerTest, single_byte_receives) {
    // Should not crash!
    std::string msg = messageStringOfAtLeast(MAX_ADB_MESSAGE_PAYLOAD * 2);
    for (int i = 0; i < msg.size(); i++) {
        logger_->Observe(msg.data() + i, 1);
    }

    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr("\"command\":\"WRTE"));
}

TEST_F(AdbMessageLoggerTest, many_bytes_receives) {
    std::string msg = messageStringOfAtLeast(MAX_ADB_MESSAGE_PAYLOAD * 2);

    // Deliver in random sized chunks..
    while (msg.size() > 0) {
        int div = (msg.size() / 2) + 1;
        int toSend = std::min<int>(rand() % div + 1, msg.size());
        logger_->Observe(msg.data(), toSend);
        msg.erase(0, toSend);
    }

    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr("\"command\":\"WRTE"));
}

TEST_F(AdbMessageLoggerTest, large_garbage_triggers_invalid_header) {
    MockAdbPacketCallback mock_cb;
    logger_->SetCallback(&mock_cb);

    EXPECT_CALL(mock_cb, OnOutOfSync(StartsWith("Invalid header"))).Times(1);

    std::string large_data(1024 * 1024 + 1, 'a');
    logger_->Observe(large_data.data(), large_data.size());
}

TEST_F(AdbMessageLoggerTest, packet_too_large_triggers_out_of_sync) {
    MockAdbPacketCallback mock_cb;
    logger_->SetCallback(&mock_cb);

    EXPECT_CALL(mock_cb, OnOutOfSync(StartsWith("Packet too large"))).Times(1);

    goldfish::adb::AMessage message;
    message.command = (uint32_t)AdbWireMessage::A_CNXN;
    message.arg0 = 0;
    message.arg1 = 0;
    message.data_length = 1024 * 1024 + 1;
    message.magic = message.command ^ 0xffffffff;

    std::string wire((char*)&message, sizeof(message));
    logger_->Observe(wire.data(), wire.size());
}

TEST_F(AdbMessageLoggerTest, invalid_header_triggers_out_of_sync) {
    MockAdbPacketCallback mock_cb;
    logger_->SetCallback(&mock_cb);

    EXPECT_CALL(mock_cb, OnOutOfSync(StartsWith("Invalid header"))).Times(1);

    goldfish::adb::AMessage message;
    message.command = (uint32_t)AdbWireMessage::A_CNXN;
    message.arg0 = 0;
    message.arg1 = 0;
    message.data_length = 0;
    message.magic = 0;

    std::string wire((char*)&message, sizeof(message));
    logger_->Observe(wire.data(), wire.size());
}

}  // namespace
