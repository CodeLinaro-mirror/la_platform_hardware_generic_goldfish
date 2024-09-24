// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/emulation/control/adb/AdbMessageLogger.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "aemu/base/testing/TestUtils.h"
#include "host-common/logging.h"

namespace {

using android::emulation::control::AdbMessageLogger;
using android::emulation::control::amessage;
using android::emulation::control::apacket;
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
    amessage message = {(uint32_t)cmd,        arg0, arg1,
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
    amessage message{0, 1, 2, 3, 4, 5};
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

void initlogs_once() {
    static bool mInitialized = false;
    if (!mInitialized) absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
    mInitialized = true;
}

static absl::once_flag initlogs;
class AdbMessageLoggerTest : public ::testing::Test {
  protected:
    AdbMessageLoggerTest() { absl::call_once(initlogs, initlogs_once); }
    void SetUp() override {
        // Add the CaptureLogSink
        log_sink_ = std::make_unique<CaptureLogSink>();
        absl::AddLogSink(log_sink_.get());
        absl::SetVLogLevel("*", 2);
        logger_ = std::make_unique<AdbMessageLogger>("test> ");
    }

    void TearDown() override {
        // Remove the CaptureLogSink
        absl::RemoveLogSink(log_sink_.get());
        logger_.reset();
    }

    void sendMessage(Message msg) { logger_->observe(msg.data(), msg.size()); }

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
    sendMessage(smallMsg() + std::string(1024 * 1024, 'a'));

    EXPECT_THAT(log_sink_->captured_log_,
                testing::HasSubstr("Adb stream out of sync, disabling further logging"));

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
        logger_->observe(msg.data() + i, 1);
    }

    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr("\"command\":\"WRTE"));
}

TEST_F(AdbMessageLoggerTest, many_bytes_receives) {
    std::string msg = messageStringOfAtLeast(MAX_ADB_MESSAGE_PAYLOAD * 2);

    // Deliver in random sized chunks..
    while (msg.size() > 0) {
        int div = (msg.size() / 2) + 1;
        int toSend = std::min<int>(rand() % div + 1, msg.size());
        logger_->observe(msg.data(), toSend);
        msg.erase(0, toSend);
    }

    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr("\"command\":\"WRTE"));
}

}  // namespace
