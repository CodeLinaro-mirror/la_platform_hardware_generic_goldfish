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

#include <gtest/gtest.h>

#include "android/sockets/socket_utils.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

TEST(ModemServiceTest, ConnectToSimulatorSendsRegistration) {
    int server_fd = android::base::socketTcp4LoopbackServer(0);
    ASSERT_GE(server_fd, 0);

    int port = android::base::socketGetPort(server_fd);
    ASSERT_GT(port, 0);

    ModemServiceImpl service(port);
    auto client_fd = service.ConnectToSimulator();
    ASSERT_TRUE(client_fd.valid());
    int client_fd_raw = client_fd.get();
    ASSERT_GE(client_fd_raw, 0);

    int conn_fd = android::base::socketAcceptAny(server_fd);
    ASSERT_GE(conn_fd, 0);

    char buf[5] = {0};
    ssize_t read_bytes = android::base::socketRecv(conn_fd, buf, 4);
    EXPECT_EQ(read_bytes, 4);
    EXPECT_STREQ(buf, "REM0");

    android::base::socketClose(conn_fd);
    android::base::socketClose(server_fd);
}

TEST(ModemServiceTest, ConstructorChecksInvalidPort) {
    EXPECT_DEATH(ModemServiceImpl(0), "Invalid modem simulator port: 0");
    EXPECT_DEATH(ModemServiceImpl(-1), "Invalid modem simulator port: -1");
}

TEST(ModemServiceTest, MethodsReturnUnimplemented) {
    ModemServiceImpl service(1234);
    ::grpc::ServerContext context;

    auto checkUnimplemented = [](const ::grpc::Status& status) {
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNIMPLEMENTED);
        EXPECT_EQ(status.error_message(), "Not implemented yet");
    };

    CellInfo cellInfoRequest;
    CellInfo cellInfoResponse;
    checkUnimplemented(service.setCellInfo(&context, &cellInfoRequest, &cellInfoResponse));

    ::google::protobuf::Empty emptyRequest;
    checkUnimplemented(service.getCellInfo(&context, &emptyRequest, &cellInfoResponse));

    Call callRequest;
    Call callResponse;
    checkUnimplemented(service.createCall(&context, &callRequest, &callResponse));
    checkUnimplemented(service.updateCall(&context, &callRequest, &callResponse));

    ::google::protobuf::Empty emptyResponse;
    checkUnimplemented(service.deleteCall(&context, &callRequest, &emptyResponse));

    ActiveCalls activeCallsResponse;
    checkUnimplemented(service.listCalls(&context, &emptyRequest, &activeCallsResponse));

    SmsMessage smsRequest;
    checkUnimplemented(service.receiveSms(&context, &smsRequest, &emptyResponse));

    checkUnimplemented(service.updateClock(&context, &emptyRequest, &emptyResponse));

    checkUnimplemented(service.receivePhoneEvents(&context, &emptyRequest, nullptr));
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android