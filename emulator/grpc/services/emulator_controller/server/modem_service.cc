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

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "android/sockets/socket_utils.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

ModemServiceImpl::ModemServiceImpl(int modem_simulator_port)
        : simulator_port_(modem_simulator_port) {
    CHECK(simulator_port_ > 0) << "Invalid modem simulator port: " << simulator_port_;
}

android::base::ScopedSocket ModemServiceImpl::ConnectToSimulator() const {
    // Try IPv4 first
    android::base::ScopedSocket fd(android::base::socketTcp4LoopbackClient(simulator_port_));

    // If IPv4 fails, try IPv6
    if (!fd.valid()) {
        VLOG(1) << "IPv4 connection failed, trying IPv6 for port " << simulator_port_;
        fd.reset(android::base::socketTcp6LoopbackClient(simulator_port_));
    }

    if (!fd.valid()) {
        LOG(ERROR) << "Failed to connect to modem simulator on port " << simulator_port_
                   << " (tried IPv4 and IPv6)";
        return {};
    }

    // Send the "REM0" registration sequence to attach as a remote client
    if (!android::base::socketSendAll(fd.get(), "REM0", 4)) {
        LOG(ERROR) << "Failed to send REM0 handshake to modem simulator";
        return {};
    }

    VLOG(1) << "Successfully connected to modem simulator on port " << simulator_port_;
    return fd;
}

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