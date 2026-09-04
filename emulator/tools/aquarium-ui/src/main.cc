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

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"

#include "android/base/bazel_info.h"
#include "goldfish/aquarium/aquarium_ui_server.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/grpcweb/grpc_web_server.h"
#include "goldfish/http/http_request.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"

ABSL_FLAG(std::string, http_address, "0.0.0.0", "HTTP host/IP address to bind and listen on");
ABSL_FLAG(uint16_t, http_port, 8085, "HTTP port to bind and listen on");
ABSL_FLAG(std::string, static_dir, "",
          "Directory containing Aquarium web UI assets. If empty, defaults to runfiles or source "
          "tree location.");
ABSL_FLAG(std::string, grpc_target, "",
          "Upstream gRPC server address (host:port or unix:path). If empty and no --discovery_file "
          "is set, the first discovered emulator will be used.");
ABSL_FLAG(std::string, discovery_file, "",
          "Path to the emulator discovery file (.ini). If empty and no --grpc_target is set, the "
          "first discovered emulator will be used.");
ABSL_FLAG(std::string, grpc_token, "", "Authentication token for the emulator gRPC service.");
ABSL_FLAG(std::string, allow_origin, "*", "CORS Access-Control-Allow-Origin header");
ABSL_FLAG(bool, access_log, false, "Enable structured HTTP access logging for every request");
ABSL_FLAG(bool, verbose, false, "Enable verbose logging");

namespace {

absl::Notification g_shutdown;

void HandleSignal(int /*sig*/) {
    if (!g_shutdown.HasBeenNotified()) {
        g_shutdown.Notify();
    }
}

}  // namespace

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage(
            "Aquarium UI Host: Unified web server hosting Aquarium React UI and gRPC-Web proxy.");
    absl::ParseCommandLine(argc, argv);
    android::base::Bazel::StoreCommandLineArgs(argc, argv);
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    const std::string host = absl::GetFlag(FLAGS_http_address);
    const uint16_t port = absl::GetFlag(FLAGS_http_port);
    const std::string grpc_target = absl::GetFlag(FLAGS_grpc_target);
    const std::string discovery_file = absl::GetFlag(FLAGS_discovery_file);
    const std::string grpc_token = absl::GetFlag(FLAGS_grpc_token);
    const std::string allow_origin = absl::GetFlag(FLAGS_allow_origin);
    const std::filesystem::path static_dir =
            goldfish::aquarium::LocateStaticDir(absl::GetFlag(FLAGS_static_dir));

    auto target_and_channel_or = goldfish::aquarium::ResolveGrpcTargetAndChannel(
            grpc_target, grpc_token, discovery_file);
    if (!target_and_channel_or.ok()) {
        LOG(ERROR) << "Failed to resolve upstream gRPC target: " << target_and_channel_or.status();
        return 1;
    }
    auto [resolved_target, channel] = std::move(*target_and_channel_or);

    LOG(INFO) << "🐠 Starting Aquarium UI Server...";
    LOG(INFO) << "  Static UI Dir:        " << static_dir;
    LOG(INFO) << "  Upstream gRPC target: " << resolved_target;
    LOG(INFO) << "  HTTP listener:        http://" << host << ":" << port;

    auto uv_loop = goldfish::async::LibuvEventLoop::Create("AquariumLoop");
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

    // Create and start GrpcWebServer with static file serving
    auto server_or = goldfish::grpcweb::GrpcWebServer::Create(
            loop.get(), socket_factory, endpoint, channel, allow_origin, nullptr,
            [static_dir](const goldfish::http::HttpRequest& req) {
                return goldfish::aquarium::ServeStaticFile(static_dir, req.Path());
            });
    if (!server_or.ok()) {
        LOG(ERROR) << "Failed to initialize GrpcWebServer: " << server_or.status();
        return 1;
    }

    auto server = std::move(*server_or);
    LOG(INFO) << "🚀 Aquarium UI is live at http://" << host << ":" << port;

    g_shutdown.WaitForNotification();

    LOG(INFO) << "Shutting down Aquarium UI server...";
    server->Stop();
    return 0;
}
