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

namespace android {
namespace emulation {
namespace control {
namespace incubating {

::grpc::Status ModemServiceImpl::setCellInfo(::grpc::ServerContext* /*context*/,
                                             const CellInfo* /*request*/, CellInfo* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::getCellInfo(::grpc::ServerContext* /*context*/,
                                             const ::google::protobuf::Empty* /*request*/,
                                             CellInfo* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::createCall(::grpc::ServerContext* /*context*/,
                                            const Call* /*request*/, Call* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::updateCall(::grpc::ServerContext* /*context*/,
                                            const Call* /*request*/, Call* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::deleteCall(::grpc::ServerContext* /*context*/,
                                            const Call* /*request*/,
                                            ::google::protobuf::Empty* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::listCalls(::grpc::ServerContext* /*context*/,
                                           const ::google::protobuf::Empty* /*request*/,
                                           ActiveCalls* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::receiveSms(::grpc::ServerContext* /*context*/,
                                            const SmsMessage* /*request*/,
                                            ::google::protobuf::Empty* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::updateClock(::grpc::ServerContext* /*context*/,
                                             const ::google::protobuf::Empty* /*request*/,
                                             ::google::protobuf::Empty* /*response*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

::grpc::Status ModemServiceImpl::receivePhoneEvents(::grpc::ServerContext* /*context*/,
                                                    const ::google::protobuf::Empty* /*request*/,
                                                    ::grpc::ServerWriter<PhoneEvent>* /*writer*/) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android