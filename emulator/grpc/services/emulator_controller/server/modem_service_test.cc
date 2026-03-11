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

namespace android {
namespace emulation {
namespace control {
namespace incubating {

TEST(ModemServiceTest, MethodsReturnUnimplemented) {
    ModemServiceImpl service;
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