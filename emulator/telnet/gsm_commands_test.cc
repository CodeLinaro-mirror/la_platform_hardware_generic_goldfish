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

#include "gsm_commands.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "console_context.h"
#include "modem_service_mock.grpc.pb.h"

namespace goldfish::telnet {
namespace {

using testing::_;

struct MockConsoleContext : public ConsoleContext {
    explicit MockConsoleContext(int port) : ConsoleContext(port) {}

    absl::StatusOr<std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface>>
    ModemStub() override {
        if (mock_modem_stub) {
            return std::move(mock_modem_stub);
        }
        return ConsoleContext::ModemStub();
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface> mock_modem_stub;
};

class GsmCommandsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        builder_ = std::make_unique<CommandRegistryBuilder>("/dummy/token");
        auto gsm = builder_->Command("gsm", "GSM commands");
        RegisterGsmCommands(gsm);
        registry_ = builder_->Build();
    }

    std::unique_ptr<CommandRegistryBuilder> builder_;
    std::unique_ptr<CommandRegistry> registry_;
};

TEST_F(GsmCommandsTest, GsmListNoCallsReturnsEmpty) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, listCalls(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::incubating::ActiveCalls* response) {
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm list", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmStatusReturnsVoiceAndDataStatus) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, getCellInfo(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::incubating::CellInfo* response) {
                response->set_cell_status_voice(
                        android::emulation::control::incubating::CellInfo::CELL_STATUS_HOME);
                response->set_cell_status_data(
                        android::emulation::control::incubating::CellInfo::CELL_STATUS_ROAMING);
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm status", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "gsm voice state: home\r\ngsm data state:  roaming");
}

TEST_F(GsmCommandsTest, GsmCallCreatesInboundCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, createCall(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::incubating::Call& request,
                         android::emulation::control::incubating::Call* response) {
                EXPECT_EQ(request.number(), "1234567");
                EXPECT_EQ(request.direction(),
                          android::emulation::control::incubating::Call::CALL_DIRECTION_INBOUND);
                EXPECT_EQ(request.state(),
                          android::emulation::control::incubating::Call::CALL_STATE_INCOMING);
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm call 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmCancelDeletesCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, deleteCall(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::incubating::Call& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.number(), "1234567");
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm cancel 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmDataModifiesDataState) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, setCellInfo(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::incubating::CellInfo& request,
                         android::emulation::control::incubating::CellInfo* response) {
                EXPECT_EQ(request.cell_status_data(),
                          android::emulation::control::incubating::CellInfo::CELL_STATUS_ROAMING);
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm data roaming", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmSignalSetsRssi) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_modem = std::make_unique<android::emulation::control::incubating::MockModemStub>();
    EXPECT_CALL(*mock_modem, setCellInfo(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::incubating::CellInfo& request,
                         android::emulation::control::incubating::CellInfo* response) {
                EXPECT_EQ(request.cell_signal_strength().rssi(), 15);
                return grpc::Status::OK;
            });
    ctx.mock_modem_stub = std::move(mock_modem);

    auto result = (*registry_)("gsm signal 15", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

}  // namespace
}  // namespace goldfish::telnet
