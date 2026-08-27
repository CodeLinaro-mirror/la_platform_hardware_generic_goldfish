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
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "android/emulation/control/grpc_channel_factory.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/grpcweb/grpc_web_server.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"

ABSL_FLAG(std::string, http_address, "0.0.0.0", "HTTP host/IP address to bind and listen on");
ABSL_FLAG(uint16_t, http_port, 8080, "HTTP port to bind and listen on");
ABSL_FLAG(std::string, grpc_target, "",
          "Upstream gRPC server address (host:port or unix:path). If empty and no --discovery_file "
          "is set, the first discovered emulator will be used.");
ABSL_FLAG(std::string, discovery_file, "",
          "Path to the emulator discovery file (.ini). If empty and no --grpc_target is set, the "
          "first discovered emulator will be used.");
ABSL_FLAG(std::string, grpc_token, "", "Authentication token for the emulator gRPC service.");
ABSL_FLAG(std::string, allow_origin, "*", "CORS Access-Control-Allow-Origin header");
ABSL_FLAG(bool, access_log, false, "Enable structured HTTP access logging for every request");
ABSL_FLAG(bool, verbose, false, "Enable verbose logging (equivalent to --v=1)");
ABSL_FLAG(int, verbosity, 0,
          "Verbosity level (equivalent to --v). 1 for RPC requests/responses, 2 for headers and "
          "frames, 3 for raw byte chunks");

namespace {

using ::android::emulation::control::EmulatorGrpcClientBuilder;
using ::android::emulation::control::GrpcChannelFactory;
using ::android::emulation::remote::Endpoint;

absl::StatusOr<std::pair<std::string, std::shared_ptr<grpc::Channel>>> ResolveGrpcTargetAndChannel(
        const std::string& target, const std::string& token, const std::string& discovery_file) {
    EmulatorGrpcClientBuilder builder;
    if (!target.empty()) {
        Endpoint endpoint;
        endpoint.set_target(target);
        if (!token.empty()) {
            auto* header = endpoint.add_required_headers();
            header->set_key("authorization");
            header->set_value("Bearer " + token);
        }
        builder.WithEndpoint(endpoint);
    } else if (!discovery_file.empty()) {
        builder.WithDiscoveryFile(discovery_file);
    } else {
        builder.ForDiscoveredEmulator({});
    }

    auto client = builder.BuildBlocking();
    if (!client.ok()) {
        return client.status();
    }

    const auto& endpoint = (*client)->GetEndpoint();
    std::string resolved_target = endpoint.target();
    GrpcChannelFactory factory(endpoint, {});
    auto channel = factory.CreateChannel();
    if (!channel) {
        return absl::InternalError("Failed to create gRPC channel for target: " + resolved_target);
    }
    return std::make_pair(std::move(resolved_target), std::move(channel));
}

absl::Notification g_shutdown;

void HandleSignal(int /*sig*/) {
    if (!g_shutdown.HasBeenNotified()) {
        g_shutdown.Notify();
    }
}

}  // namespace

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage(
            "High-performance native C++ in-process gRPC-Web proxy for the Android Emulator.");
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

    int verbosity = absl::GetFlag(FLAGS_verbosity);
    if (absl::GetFlag(FLAGS_verbose) && verbosity == 0) {
        verbosity = 1;
    }
    if (verbosity > 0) {
        absl::SetGlobalVLogLevel(verbosity);
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const std::string host = absl::GetFlag(FLAGS_http_address);
    const uint16_t port = absl::GetFlag(FLAGS_http_port);
    const std::string grpc_target = absl::GetFlag(FLAGS_grpc_target);
    const std::string discovery_file = absl::GetFlag(FLAGS_discovery_file);
    const std::string grpc_token = absl::GetFlag(FLAGS_grpc_token);
    const std::string allow_origin = absl::GetFlag(FLAGS_allow_origin);

    auto target_and_channel_or =
            ResolveGrpcTargetAndChannel(grpc_target, grpc_token, discovery_file);
    if (!target_and_channel_or.ok()) {
        LOG(ERROR) << "Failed to resolve upstream gRPC target: " << target_and_channel_or.status();
        return 1;
    }
    auto [resolved_target, channel] = std::move(*target_and_channel_or);

    LOG(INFO) << "Starting gRPC-Web proxy...";
    LOG(INFO) << "  Upstream gRPC target: " << resolved_target;
    LOG(INFO) << "  HTTP listener:        " << host << ":" << port;
    LOG(INFO) << "  Allowed CORS Origin:  " << allow_origin;
    if (verbosity > 0) {
        LOG(INFO) << "  Verbosity level:      " << verbosity;
    }

    auto uv_loop = goldfish::async::LibuvEventLoop::Create("GrpcWebLoop");
    auto loop = goldfish::async::ThreadedEventLoop::Create(std::move(uv_loop));
    if (!loop) {
        LOG(ERROR) << "Failed to start ThreadedEventLoop";
        return 1;
    }
    auto socket_factory = std::make_shared<goldfish::async::LibuvAsyncSocketFactory>();

    auto ip_opt = goldfish::network::ToIpAddress(host);
    if (!ip_opt.ok()) {
        LOG(ERROR) << "Invalid IP address: " << host << ": " << ip_opt.status();
        return 1;
    }
    auto endpoint = goldfish::network::ToEndpoint(*ip_opt, port);

    goldfish::http::AccessLogger access_logger = nullptr;
    if (absl::GetFlag(FLAGS_access_log) || verbosity > 0) {
        access_logger = [](const goldfish::http::AccessLogEntry& e) {
            LOG(INFO) << goldfish::network::ToString(e.client_endpoint) << " "
                      << goldfish::http::HttpMethodToString(e.method) << " " << e.path << " -> "
                      << e.status_code << " (" << e.bytes_sent << " bytes) in "
                      << absl::FormatDuration(e.duration);
        };
    }

    auto server_or = goldfish::grpcweb::GrpcWebServer::Create(
            loop.get(), socket_factory, endpoint, channel, allow_origin, std::move(access_logger));
    if (!server_or.ok()) {
        LOG(ERROR) << "Failed to initialize GrpcWebServer: " << server_or.status();
        return 1;
    }

    auto server = std::move(*server_or);
    LOG(INFO) << "gRPC-Web proxy listening on "
              << goldfish::network::ToString(server->GetEndpoint());

    g_shutdown.WaitForNotification();

    LOG(INFO) << "Shutting down gRPC-Web proxy...";
    server->Stop();
    return 0;
}
