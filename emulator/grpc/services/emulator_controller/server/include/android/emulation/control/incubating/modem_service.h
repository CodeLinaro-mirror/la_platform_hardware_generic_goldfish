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
#pragma once

#include <grpcpp/grpcpp.h>

#include "android/sockets/scoped_socket.h"
#include "modem_service.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

class ModemServiceImpl final : public Modem::Service {
  public:
    explicit ModemServiceImpl(int modem_simulator_port = 0);
    ~ModemServiceImpl() override = default;

    // Connects to the Modem Simulator and returns the file descriptor.
    // The caller takes ownership of the file descriptor.
    android::base::ScopedSocket ConnectToSimulator() const;

    ::grpc::Status setCellInfo(::grpc::ServerContext* context, const CellInfo* request,
                               CellInfo* response) override;
    ::grpc::Status getCellInfo(::grpc::ServerContext* context,
                               const ::google::protobuf::Empty* request,
                               CellInfo* response) override;
    ::grpc::Status createCall(::grpc::ServerContext* context, const Call* request,
                              Call* response) override;
    ::grpc::Status updateCall(::grpc::ServerContext* context, const Call* request,
                              Call* response) override;
    ::grpc::Status deleteCall(::grpc::ServerContext* context, const Call* request,
                              ::google::protobuf::Empty* response) override;
    ::grpc::Status listCalls(::grpc::ServerContext* context,
                             const ::google::protobuf::Empty* request,
                             ActiveCalls* response) override;
    ::grpc::Status receiveSms(::grpc::ServerContext* context, const SmsMessage* request,
                              ::google::protobuf::Empty* response) override;
    ::grpc::Status updateClock(::grpc::ServerContext* context,
                               const ::google::protobuf::Empty* request,
                               ::google::protobuf::Empty* response) override;
    ::grpc::Status receivePhoneEvents(::grpc::ServerContext* context,
                                      const ::google::protobuf::Empty* request,
                                      ::grpc::ServerWriter<PhoneEvent>* writer) override;

  private:
    int simulator_port_;
};

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android