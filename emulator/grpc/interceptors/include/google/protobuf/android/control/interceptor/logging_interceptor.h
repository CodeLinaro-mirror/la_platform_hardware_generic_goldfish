// Copyright (C) 2019 The Android Open Source Project
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

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace google::protobuf {
class Message;
}  // namespace google::protobuf

namespace android::control::interceptor {

using grpc::experimental::ClientInterceptorFactoryInterface;
using grpc::experimental::InterceptionHookPoints;
using grpc::experimental::ServerInterceptorFactoryInterface;

enum class CallType : uint8_t {
    kUnary,
    kClientStreaming,
    kServerStreaming,
    kBidiStreaming,
    kUnknown
};

enum class Direction : uint8_t {
    kIncoming /* server receives */,
    kOutgoing /* Client calls */
};
using InvocationRecord = struct InvocationRecord {
    std::string method = "unknown";          // Invoked method.
    std::string incoming = "...";            // Shortened receive parameters.
    std::string response = "...";            // Shortened response string.
    grpc::Status status = grpc::Status::OK;  // Status

    uint64_t rcv_messages = 0;  // Number of messages received
    uint64_t rcv_bytes = 0;     // Size of all received protobuf messages.
    uint64_t rcv_time = 0;      // Time spend receiving bytes out over the wire.

    uint64_t snd_messages = 0;  // Number of messages send
    uint64_t snd_bytes = 0;     // Size of all send protobuf messages.
    uint64_t snd_time = 0;      // Time spend sending bytes out over the wire.

    uint64_t duration = 0;                      // Total lifetime of the request.
    Direction direction = Direction::kIncoming;  // Incoming (server) or outgoing (client)
    CallType type = CallType::kUnary;
    std::string peer;  // The peer (the other side of this request)

    // Timestamps of the various stages. We will use NUM_INTERCEPTION_HOOKS to
    // store the creation time
    uint64_t timestamps[static_cast<int>(InterceptionHookPoints::NUM_INTERCEPTION_HOOKS) + 1] = {};

    static const std::array<std::string, 4> kTypes;
    static const int kStartTimeIdx =
            static_cast<int>(InterceptionHookPoints::NUM_INTERCEPTION_HOOKS);
};

using ReportingFunction = std::function<void(const InvocationRecord&)>;

// A simple logging interceptor that collects InvocationRecords.
// The reporting function will be invoked when the gRPC method is completed
// and the interceptor object is deleted.
//
// You can use it as follows:
// - Create a ReportingFunction
// - Register the reporting function with gRPC
//
//    ServerBuilder builder;
//    std::vector<std::unique_ptr<ServerInterceptorFactoryInterface>> creators;
//    creators.emplace_back(std::make_unique<LoggingInterceptorFactory>([](auto
//    log}{ .... }));
//    builder.experimental().SetInterceptorCreators(std::move(creators));
//
class LoggingInterceptor : public grpc::experimental::Interceptor {
  public:
    LoggingInterceptor(grpc::experimental::ServerRpcInfo* info, ReportingFunction reporter);
    LoggingInterceptor(grpc::experimental::ClientRpcInfo* info, ReportingFunction reporter);
    ~LoggingInterceptor() override;

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;

  private:
    std::string ChopStr(std::string) const;
    std::string FormatProtobufMessage(const ::google::protobuf::Message* msg) const;

    // We will cut off all response/incoming strings at this length.
    static constexpr unsigned int kMaxStringLen = 80;

    // Max field length of protobuf message we are willing to log.
    static constexpr unsigned int kMaxProtobufStrLen = 20;

    // Maximum size of a protobuf message we are willing to log.
    static constexpr unsigned int kMaxProtobufMsgLogSize = 2048;

    InvocationRecord loginfo_;
    ReportingFunction reporter_;
    grpc::experimental::ClientRpcInfo* client_info_;
    grpc::experimental::ServerRpcInfo* server_info_;
};

// The factory class that needs to be registered with the gRPC server/client.
class LoggingInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface,
                                  public grpc::experimental::ClientInterceptorFactoryInterface {
  public:
    explicit LoggingInterceptorFactory(ReportingFunction reporter);
    ~LoggingInterceptorFactory() override = default;
    grpc::experimental::Interceptor* CreateServerInterceptor(
            grpc::experimental::ServerRpcInfo* info) override;
    grpc::experimental::Interceptor* CreateClientInterceptor(
            grpc::experimental::ClientRpcInfo* info) override;

  private:
    ReportingFunction reporter_;
};

// A logging interceptor that logs all the requests to stdout using LOG(INFO)
class StdOutLoggingInterceptorFactory : public LoggingInterceptorFactory {
  public:
    StdOutLoggingInterceptorFactory();
};

}  // namespace android::control::interceptor
