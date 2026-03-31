// Copyright 2026 The Android Open Source Project
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
#include "android/emulation/control/incubating/modem_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "goldfish/modem_simulator/i_modem_simulator_client.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

using ::testing::_;
using ::testing::Return;

class MockModemSimulatorClient : public goldfish::modem_simulator::IModemSimulatorClient {
  public:
    MOCK_METHOD(absl::StatusOr<CellInfo>, SetCellInfo, (const CellInfo&), (override));
    MOCK_METHOD(absl::StatusOr<CellInfo>, GetCellInfo, (), (override));
    MOCK_METHOD(absl::StatusOr<Call>, CreateCall, (const Call&), (override));
    MOCK_METHOD(absl::StatusOr<Call>, UpdateCall, (const Call&), (override));
    MOCK_METHOD(absl::Status, DeleteCall, (const Call&), (override));
    MOCK_METHOD(absl::StatusOr<std::vector<Call>>, ListCalls, (), (override));
    MOCK_METHOD(absl::Status, ReceiveSmsUtf8, (std::string_view, std::string_view), (override));
    MOCK_METHOD(absl::Status, ReceiveSmsEncoded, (std::vector<uint8_t>), (override));
    MOCK_METHOD(absl::Status, UpdateClock, (), (override));
};

class ModemServiceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto mock_client = std::make_unique<MockModemSimulatorClient>();
        client_ = mock_client.get();
        service_ = std::make_unique<ModemServiceImpl>(std::move(mock_client));
    }

    MockModemSimulatorClient* client_;
    std::unique_ptr<ModemServiceImpl> service_;
};

TEST_F(ModemServiceTest, SetCellInfo) {
    ::grpc::ServerContext context;
    CellInfo request;
    CellInfo response;

    goldfish::modem_simulator::IModemSimulatorClient::CellInfo mock_response;
    mock_response.standard = goldfish::modem_simulator::IModemSimulatorClient::CellStandard::LTE;

    EXPECT_CALL(*client_, SetCellInfo(_)).WillOnce(Return(mock_response));

    auto status = service_->setCellInfo(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.cell_standard(), CellInfo::CELL_STANDARD_LTE);
}

TEST_F(ModemServiceTest, GetCellInfo) {
    ::grpc::ServerContext context;
    ::google::protobuf::Empty request;
    CellInfo response;

    goldfish::modem_simulator::IModemSimulatorClient::CellInfo mock_response;
    mock_response.standard = goldfish::modem_simulator::IModemSimulatorClient::CellStandard::GSM;

    EXPECT_CALL(*client_, GetCellInfo()).WillOnce(Return(mock_response));

    auto status = service_->getCellInfo(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.cell_standard(), CellInfo::CELL_STANDARD_GSM);
}

TEST_F(ModemServiceTest, CreateCall) {
    ::grpc::ServerContext context;
    Call request;
    request.set_number("12345");
    Call response;

    goldfish::modem_simulator::IModemSimulatorClient::Call mock_response;
    mock_response.number = "12345";
    mock_response.state = goldfish::modem_simulator::IModemSimulatorClient::CallState::ACTIVE;

    EXPECT_CALL(*client_, CreateCall(_)).WillOnce(Return(mock_response));

    auto status = service_->createCall(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.number(), "12345");
    EXPECT_EQ(response.state(), Call::CALL_STATE_ACTIVE);
}

TEST_F(ModemServiceTest, UpdateCall) {
    ::grpc::ServerContext context;
    Call request;
    Call response;

    goldfish::modem_simulator::IModemSimulatorClient::Call mock_response;
    mock_response.number = "54321";

    EXPECT_CALL(*client_, UpdateCall(_)).WillOnce(Return(mock_response));

    auto status = service_->updateCall(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.number(), "54321");
}

TEST_F(ModemServiceTest, DeleteCall) {
    ::grpc::ServerContext context;
    Call request;
    ::google::protobuf::Empty response;

    EXPECT_CALL(*client_, DeleteCall(_)).WillOnce(Return(absl::OkStatus()));

    auto status = service_->deleteCall(&context, &request, &response);

    EXPECT_TRUE(status.ok());
}

TEST_F(ModemServiceTest, ListCalls) {
    ::grpc::ServerContext context;
    ::google::protobuf::Empty request;
    ActiveCalls response;

    std::vector<goldfish::modem_simulator::IModemSimulatorClient::Call> mock_calls(2);
    mock_calls[0].number = "111";
    mock_calls[1].number = "222";

    EXPECT_CALL(*client_, ListCalls()).WillOnce(Return(mock_calls));

    auto status = service_->listCalls(&context, &request, &response);

    EXPECT_TRUE(status.ok());
    ASSERT_EQ(response.calls_size(), 2);
    EXPECT_EQ(response.calls(0).number(), "111");
    EXPECT_EQ(response.calls(1).number(), "222");
}

TEST_F(ModemServiceTest, ReceiveSmsUtf8) {
    ::grpc::ServerContext context;
    SmsMessage request;
    request.set_number("123");
    request.set_text("hello");
    ::google::protobuf::Empty response;

    EXPECT_CALL(*client_, ReceiveSmsUtf8("123", "hello")).WillOnce(Return(absl::OkStatus()));

    auto status = service_->receiveSms(&context, &request, &response);

    EXPECT_TRUE(status.ok());
}

TEST_F(ModemServiceTest, ReceiveSmsEncoded) {
    ::grpc::ServerContext context;
    SmsMessage request;
    request.set_number("123");
    request.set_encodedmessage("0123");  // 2 bytes
    ::google::protobuf::Empty response;

    std::vector<uint8_t> expected_bytes = {0x01, 0x23};
    EXPECT_CALL(*client_, ReceiveSmsEncoded(expected_bytes)).WillOnce(Return(absl::OkStatus()));

    auto status = service_->receiveSms(&context, &request, &response);

    EXPECT_TRUE(status.ok());
}

TEST_F(ModemServiceTest, UpdateClock) {
    ::grpc::ServerContext context;
    ::google::protobuf::Empty request;
    ::google::protobuf::Empty response;

    EXPECT_CALL(*client_, UpdateClock()).WillOnce(Return(absl::OkStatus()));

    auto status = service_->updateClock(&context, &request, &response);

    EXPECT_TRUE(status.ok());
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android