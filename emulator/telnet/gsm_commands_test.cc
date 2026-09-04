#include "gsm_commands.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "console_context.h"
#include "netsim/cell_mock.grpc.pb.h"

namespace goldfish::telnet {
namespace {

using testing::_;

struct MockConsoleContext : public ConsoleContext {
    explicit MockConsoleContext(int port) : ConsoleContext(port) {}

    absl::StatusOr<std::unique_ptr<netsim::cell::CellService::StubInterface>> NetsimCellStub()
            override {
        if (mock_cell_stub) {
            return std::move(mock_cell_stub);
        }
        return ConsoleContext::NetsimCellStub();
    }

    absl::StatusOr<uint32_t> GetCellularChipId() override { return 1; }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewNetsimContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    std::unique_ptr<netsim::cell::CellService::StubInterface> mock_cell_stub;
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

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm list", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmStatusReturnsVoiceAndDataStatus) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                response->set_voice_registration(netsim::cell::RegistrationStatus::REGISTERED_HOME);
                response->set_data_registration(netsim::cell::RegistrationStatus::ROAMING);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm status", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "gsm voice state: home\r\ngsm data state:  roaming");
}

TEST_F(GsmCommandsTest, GsmCallCreatesInboundCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_incoming_call());
                EXPECT_EQ(request.incoming_call().number(), "1234567");
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm call 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmCancelDeletesCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                auto* call = response->add_active_calls();
                call->set_number("1234567");
                call->set_state(netsim::cell::Call::ACTIVE);
                return grpc::Status::OK;
            });
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_end_call());
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm cancel 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmDataModifiesDataState) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_set_data_registration());
                EXPECT_EQ(request.set_data_registration().status(),
                          netsim::cell::RegistrationStatus::ROAMING);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm data roaming", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmSignalSetsRssi) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_set_signal_strength());
                EXPECT_EQ(request.set_signal_strength().rssi(), 15);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm signal 15", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmListWithCallsReturnsFormattedList) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                {
                    auto* call = response->add_active_calls();
                    call->set_number("1234567");
                    call->set_state(netsim::cell::Call::ACTIVE);
                    call->set_direction(netsim::cell::Call::MOBILE_TERMINATED);
                }
                {
                    auto* call = response->add_active_calls();
                    call->set_number("7654321");
                    call->set_state(netsim::cell::Call::DIALING);
                    call->set_direction(netsim::cell::Call::MOBILE_ORIGINATED);
                }
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm list", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "inbound from 1234567    : active\r\noutbound to  7654321    : dialing");
}

TEST_F(GsmCommandsTest, GsmBusyOutboundCallEndsOutboundCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                auto* call = response->add_active_calls();
                call->set_number("1234567");
                call->set_state(netsim::cell::Call::DIALING);
                call->set_direction(netsim::cell::Call::MOBILE_ORIGINATED);
                return grpc::Status::OK;
            });
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_end_call());
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm busy 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmBusyNoOutboundCallReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                // No active calls
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm busy 1234567", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(result.status().message(), "no current outbound call to number '1234567' (call 0x0)");
}

TEST_F(GsmCommandsTest, GsmHoldCallUpdatesState) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                auto* call = response->add_active_calls();
                call->set_number("1234567");
                call->set_state(netsim::cell::Call::ACTIVE);
                return grpc::Status::OK;
            });
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_remote_hold());
                EXPECT_TRUE(request.remote_hold().on_hold());
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm hold 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmAcceptCallUpdatesState) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                auto* call = response->add_active_calls();
                call->set_number("1234567");
                call->set_state(netsim::cell::Call::HOLDING);
                return grpc::Status::OK;
            });
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_remote_hold());
                EXPECT_FALSE(request.remote_hold().on_hold());
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm accept 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmVoiceModifiesVoiceState) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_set_voice_registration());
                EXPECT_EQ(request.set_voice_registration().status(),
                          netsim::cell::RegistrationStatus::ROAMING);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm voice roaming", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmVoiceInvalidStateReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm voice invalid_state", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(),
              "bad GSM voice state name, try 'help gsm voice' for list of valid values");
}

TEST_F(GsmCommandsTest, GsmSignalProfileSetsSignalStrength) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_set_signal_strength());
                EXPECT_EQ(request.set_signal_strength().rssi(), 15);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm signal-profile 3", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmMeterReturnsUnimplemented) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result_on = (*registry_)("gsm meter on", ctx);
    ASSERT_FALSE(result_on.ok());
    EXPECT_EQ(result_on.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_EQ(result_on.status().message(),
              "Metered status is not supported by netsim cellular simulation");

    auto result_off = (*registry_)("gsm meter off", ctx);
    ASSERT_FALSE(result_off.ok());
    EXPECT_EQ(result_off.status().code(), absl::StatusCode::kUnimplemented);
}

TEST_F(GsmCommandsTest, GsmAcceptIncomingCallAnswersCall) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                auto* call = response->add_active_calls();
                call->set_number("1234567");
                call->set_state(netsim::cell::Call::INCOMING);
                return grpc::Status::OK;
            });
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_remote_answer());
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm accept 1234567", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmCancelCallNotFoundReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm cancel 9999999", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(result.status().message(), "no current call to/from number '9999999'");
}

TEST_F(GsmCommandsTest, GsmHoldCallNotFoundReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Get(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const netsim::cell::GetCellRequest& request,
                         netsim::cell::Cell* response) {
                response->set_id(1);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm hold 9999999", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(result.status().message(), "no current call to/from number '9999999'");
}

TEST_F(GsmCommandsTest, GsmCallInvalidNumberFormatReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm call abc123", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "bad phone number format, use digits, # and + only");
}

TEST_F(GsmCommandsTest, GsmSignalWithBerSetsRssiAndBer) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
    EXPECT_CALL(*mock_cell, Execute(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const netsim::cell::ExecuteCellRequest& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.id(), 1);
                EXPECT_TRUE(request.has_set_signal_strength());
                EXPECT_EQ(request.set_signal_strength().rssi(), 20);
                EXPECT_EQ(request.set_signal_strength().ber(), 5);
                return grpc::Status::OK;
            });
    ctx.mock_cell_stub = std::move(mock_cell);

    auto result = (*registry_)("gsm signal 20 5", ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(GsmCommandsTest, GsmSignalInvalidRssiOrBerReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result1 = (*registry_)("gsm signal 32", ctx);
    ASSERT_FALSE(result1.ok());
    EXPECT_EQ(result1.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result1.status().message(), "invalid RSSI - must be 0..31 or 99");

    auto result2 = (*registry_)("gsm signal 15 8", ctx);
    ASSERT_FALSE(result2.ok());
    EXPECT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result2.status().message(), "invalid BER - must be 0..7 or 99");
}

TEST_F(GsmCommandsTest, GsmSignalProfileAllLevelsMapping) {
    const std::vector<std::pair<int, int>> level_to_rssi = {
        {0, 0}, {1, 5}, {2, 10}, {3, 15}, {4, 31}};

    for (const auto& [level, expected_rssi] : level_to_rssi) {
        MockConsoleContext ctx(5554);
        ctx.authenticated = true;

        auto mock_cell = std::make_unique<netsim::cell::MockCellServiceStub>();
        EXPECT_CALL(*mock_cell, Execute(_, _, _))
                .WillOnce([expected_rssi = expected_rssi](
                                  grpc::ClientContext* context,
                                  const netsim::cell::ExecuteCellRequest& request,
                                  google::protobuf::Empty* response) {
                    EXPECT_EQ(request.id(), 1);
                    EXPECT_TRUE(request.has_set_signal_strength());
                    EXPECT_EQ(request.set_signal_strength().rssi(), expected_rssi);
                    return grpc::Status::OK;
                });
        ctx.mock_cell_stub = std::move(mock_cell);

        auto result = (*registry_)(absl::StrFormat("gsm signal-profile %d", level), ctx);
        ASSERT_TRUE(result.ok()) << "Failed for level " << level << ": "
                                 << result.status().message();
        EXPECT_EQ(*result, "");
    }
}

TEST_F(GsmCommandsTest, GsmSignalProfileInvalidLevelReturnsError) {
    MockConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*registry_)("gsm signal-profile 5", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "invalid signal strength - must be 0..4");
}

}  // namespace
}  // namespace goldfish::telnet
