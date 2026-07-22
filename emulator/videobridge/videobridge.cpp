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
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"

#include "android/emulation/control/basic_token_auth.h"
#include "goldfish/videobridge/emulator_client.h"
#include "goldfish/videobridge/rtc_service.h"
#include "goldfish/videobridge/switchboard.h"
#include "media/grpc_media_provider.h"
#include "rtc_base/logging.h"

ABSL_FLAG(std::string, webrtc_log_level, "none",
          "WebRTC native logging level (verbose, info, warning, error, none).");

ABSL_FLAG(std::string, discovery_file, "",
          "Path to the emulator discovery file. If empty, the first discovered "
          "emulator will be used.");
ABSL_FLAG(std::string, grpc_address, "localhost:50051",
          "Address and port to bind the signaling gRPC service to.");
ABSL_FLAG(std::string, grpc_token, "", "Secure token required to call this server.");
ABSL_FLAG(std::string, tls_cert, "", "Path to the PEM-encoded TLS certificate file.");
ABSL_FLAG(std::string, tls_key, "", "Path to the PEM-encoded private key file.");
ABSL_FLAG(std::string, tls_ca, "", "Path to the PEM-encoded CA certificate file (enables mTLS).");
ABSL_FLAG(std::string, shared_memory_path, "",
          "Path to the shared memory file to enable MMAP transport optimization (POSIX shared "
          "memory).");

namespace goldfish::videobridge {

using ::android::emulation::control::AnyTokenAuth;
using ::android::emulation::control::BasicTokenAuth;
using ::android::emulation::control::StaticTokenAuth;

namespace {

std::string ReadFile(const std::string& path) {
    if (path.empty()) return "";
    std::ifstream stream(path);
    if (!stream) {
        LOG(ERROR) << "Failed to read file: " << path;
        return "";
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::shared_ptr<::grpc::ServerCredentials> BuildCredentials() {
    std::shared_ptr<::grpc::ServerCredentials> creds;
    const std::string cert_path = absl::GetFlag(FLAGS_tls_cert);
    const std::string key_path = absl::GetFlag(FLAGS_tls_key);
    const std::string ca_path = absl::GetFlag(FLAGS_tls_ca);

    const std::string cert_pem = ReadFile(cert_path);
    const std::string key_pem = ReadFile(key_path);
    const std::string ca_pem = ReadFile(ca_path);

    if (!cert_pem.empty() && !key_pem.empty()) {
        ::grpc::SslServerCredentialsOptions ssl_opts;
        const ::grpc::SslServerCredentialsOptions::PemKeyCertPair keycert{.private_key = key_pem,
                                                                          .cert_chain = cert_pem};
        ssl_opts.pem_key_cert_pairs.push_back(keycert);

        if (!ca_pem.empty()) {
            ssl_opts.pem_root_certs = ca_pem;
            ssl_opts.client_certificate_request =
                    GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
            LOG(INFO) << "Mutual TLS (mTLS) client verification enabled.";
        } else {
            LOG(INFO) << "TLS encryption enabled (Server authentication only).";
        }
        creds = ::grpc::SslServerCredentials(ssl_opts);
    } else {
        creds = ::grpc::InsecureServerCredentials();
        LOG(WARNING) << "Configured gRPC server with Insecure credentials.";
    }

    const std::string token = absl::GetFlag(FLAGS_grpc_token);
    if (!token.empty()) {
        auto validators = std::vector<std::unique_ptr<BasicTokenAuth>>();
        validators.emplace_back(std::make_unique<StaticTokenAuth>(token, "videobridge", nullptr));

        creds->SetAuthMetadataProcessor(
                std::make_shared<AnyTokenAuth>(std::move(validators), nullptr));
        LOG(INFO) << "Static token authentication enabled.";
    } else {
        LOG(WARNING) << "*** No gRPC protection active (token authentication is disabled) ***";
    }

    return creds;
}

class AbseilLogSink : public webrtc::LogSink {
  public:
    void OnLogMessage(const std::string& message) override {
        std::string_view msg = message;
        if (!msg.empty() && msg.back() == '\n') {
            msg.remove_suffix(1);
        }
        // LOG(INFO) << "[WebRTC] " << msg;
    }

    void OnLogMessage(const std::string& message, webrtc::LoggingSeverity severity) override {
        std::string_view msg = message;
        if (!msg.empty() && msg.back() == '\n') {
            msg.remove_suffix(1);
        }
        switch (severity) {
        case webrtc::LS_VERBOSE:
            VLOG(2) << "[WebRTC] " << msg;
            break;
        case webrtc::LS_INFO:
            VLOG(1) << "[WebRTC] " << msg;
            break;
        case webrtc::LS_WARNING:
            LOG(WARNING) << "[WebRTC] " << msg;
            break;
        case webrtc::LS_ERROR:
            LOG(ERROR) << "[WebRTC] " << msg;
            break;
        default:
            break;
        }
    }
};

static AbseilLogSink g_abseil_log_sink;

void ConfigureWebRtcLogging(const std::string& level) {
    webrtc::LoggingSeverity min_sev = webrtc::LS_WARNING;
    if (level == "verbose") {
        min_sev = webrtc::LS_VERBOSE;
    } else if (level == "info") {
        min_sev = webrtc::LS_INFO;
    } else if (level == "warning") {
        min_sev = webrtc::LS_WARNING;
    } else if (level == "error") {
        min_sev = webrtc::LS_ERROR;
    } else if (level == "none") {
        min_sev = webrtc::LS_NONE;
    }

    webrtc::LogMessage::LogToDebug(webrtc::LS_NONE);
    webrtc::LogMessage::SetLogToStderr(false);
    webrtc::LogMessage::AddLogToStream(&g_abseil_log_sink, min_sev);
}

int RunServer() {
    ConfigureWebRtcLogging(absl::GetFlag(FLAGS_webrtc_log_level));
    // 1. Initialize EmulatorClient
    auto client = std::make_shared<EmulatorClient>(absl::GetFlag(FLAGS_discovery_file));
    if (!client->Connect(absl::Seconds(5)).ok()) {
        LOG(ERROR) << "Failed to connect to emulator. Ensure emulator is running.";
        return 1;
    }

    // 2. Initialize Media Provider & Switchboard
    auto provider =
            std::make_shared<GrpcMediaProvider>(client, 0, absl::GetFlag(FLAGS_shared_memory_path));
    auto switchboard = std::make_shared<Switchboard>(client, provider);

    // 3. Initialize RtcService and Start gRPC Server
    RtcService rtc_service(switchboard);

    const std::string server_address = absl::GetFlag(FLAGS_grpc_address);
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, BuildCredentials());
    builder.RegisterService(&rtc_service);

    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        LOG(ERROR) << "Failed to build and start gRPC server on " << server_address;
        return 1;
    }
    LOG(INFO) << "Video Bridge Signaling Server listening on " << server_address;

    server->Wait();
    return 0;
}

}  // namespace

}  // namespace goldfish::videobridge

int main(int argc, char* argv[]) {
    absl::SetProgramUsageMessage(absl::StrFormat(
            "A WebRTC Video Bridge and input signaling gateway for the Android Emulator.\n\n"
            "This binary starts a gRPC server that bridges remote JSEP WebRTC signaling and input\n"
            "events (mouse, keyboard, touch) to a running Android Emulator instance. It streams "
            "the\n"
            "emulator's screen over WebRTC and forwards control actions.\n\n"
            "Usage:\n"
            "  $ videobridge --discovery_file /path/to/avd/running/pid.ini [flags]\n\n"
            "Common Flags:\n"
            "  --discovery_file       Path to the emulator's INI discovery file (finds active "
            "emulator).\n"
            "  --grpc_address         Host/port to bind the signaling gRPC service (default: "
            "localhost:50051).\n"
            "  --grpc_token           Optional authentication token required to make RPCs.\n"
            "  --shared_memory_path   Enables high-performance POSIX shared memory transport for "
            "local frame capture.\n"
            "  --webrtc_log_level     WebRTC log verbosity (verbose, info, warning, error, "
            "none).\n"));
    absl::ParseCommandLine(argc, argv);
    return goldfish::videobridge::RunServer();
}
