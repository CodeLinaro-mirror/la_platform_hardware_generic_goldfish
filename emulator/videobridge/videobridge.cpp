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
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "core/grpc_input_sender.h"
#include "goldfish/file/file.h"
#include "goldfish/videobridge/codec_factories.h"
#include "goldfish/videobridge/emulator_client.h"
#include "goldfish/videobridge/media_track_provider.h"
#include "goldfish/videobridge/rtc_service.h"
#include "goldfish/videobridge/switchboard.h"
#include "goldfish/videobridge/webrtc_logging.h"
#include "media/grpc_audio_source.h"
#include "media/grpc_video_source.h"
#include "webrtc/rtc_service_impl.h"

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
ABSL_FLAG(bool, list_codecs, false, "List the available WebRTC codecs and exit.");
ABSL_FLAG(uint32_t, width, 0, "Optional target video capture width (0 for native display width).");
ABSL_FLAG(uint32_t, height, 0,
          "Optional target video capture height (0 for native display height).");

namespace goldfish::videobridge {

using ::android::emulation::control::AnyTokenAuth;
using ::android::emulation::control::BasicTokenAuth;
using ::android::emulation::control::StaticTokenAuth;

namespace {

void PrintVideoCodecs(std::string_view heading,
                      const std::vector<webrtc::SdpVideoFormat>& formats) {
    std::cout << "[" << heading << "]\n";
    if (formats.empty()) {
        std::cout << "  None found.\n\n";
        return;
    }
    for (const auto& format : formats) {
        std::cout << "  - Codec: " << format.name << "\n";
        for (const auto& param : format.parameters) {
            std::cout << "      " << param.first << " = " << param.second << "\n";
        }
    }
    std::cout << "\n";
}

void PrintAudioCodecs(std::string_view heading, const std::vector<webrtc::AudioCodecSpec>& specs) {
    std::cout << "[" << heading << "]\n";
    if (specs.empty()) {
        std::cout << "  None found.\n";
        return;
    }
    for (const auto& spec : specs) {
        std::cout << "  - Codec: " << spec.format.name << " (Channels: " << spec.info.num_channels
                  << ", Sample Rate: " << spec.info.sample_rate_hz << "Hz)\n";
    }
}

void ListCodecs() {
    std::cout << "==========================================\n";
    std::cout << "  WebRTC Host Capabilities                \n";
    std::cout << "==========================================\n\n";

    std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory =
            CreatePlatformVideoEncoderFactory();
    std::unique_ptr<webrtc::VideoDecoderFactory> video_decoder_factory =
            CreatePlatformVideoDecoderFactory();

    PrintVideoCodecs("Video Encoders (Preference Order)",
                     video_encoder_factory->GetSupportedFormats());
    PrintVideoCodecs("Video Decoders", video_decoder_factory->GetSupportedFormats());

    auto audio_encoder_factory = ::webrtc::CreateBuiltinAudioEncoderFactory();
    PrintAudioCodecs("Audio Encoders", audio_encoder_factory->GetSupportedEncoders());
    std::cout << "\n";

    auto audio_decoder_factory = ::webrtc::CreateBuiltinAudioDecoderFactory();
    PrintAudioCodecs("Audio Decoders", audio_decoder_factory->GetSupportedDecoders());
    std::cout << "==========================================\n";
}

std::string ReadFile(const std::string& path) {
    if (path.empty()) return "";
    auto content = android::base::file::read_whole_file(path, /*binary=*/false);
    if (!content.ok()) {
        LOG(ERROR) << "Failed to read file " << path << ": " << content.status();
        return "";
    }
    return std::move(*content);
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

std::unique_ptr<InputSender> CreateGrpcInputSenderForLabel(
        const std::shared_ptr<EmulatorClient>& client, DataChannelLabel label) {
    switch (label) {
    case DataChannelLabel::kInput:
    case DataChannelLabel::kInputV2:
    case DataChannelLabel::kSensorsV2:
        return std::make_unique<GrpcInputSender>(client, label);
    case DataChannelLabel::kAdb:
        return nullptr;
    }
    return nullptr;
}

int RunServer() {
    ConfigureWebRtcLogging(absl::GetFlag(FLAGS_webrtc_log_level));
    auto client = std::make_shared<EmulatorClient>(absl::GetFlag(FLAGS_discovery_file));
    if (!client->Connect(absl::Seconds(5)).ok()) {
        LOG(ERROR) << "Failed to connect to emulator. Ensure emulator is running.";
        return 1;
    }

    GrpcVideoSourceOptions video_options;
    video_options.display_id = 0;
    video_options.width = absl::GetFlag(FLAGS_width);
    video_options.height = absl::GetFlag(FLAGS_height);
    if (!absl::GetFlag(FLAGS_shared_memory_path).empty()) {
        video_options.transport = GrpcVideoSourceOptions::Transport::kSharedMemory;
        video_options.shared_memory_path =
                std::filesystem::path(absl::GetFlag(FLAGS_shared_memory_path));
    } else {
        video_options.transport = GrpcVideoSourceOptions::Transport::kGrpcBytes;
    }

    auto video_source = ::webrtc::make_ref_counted<GrpcVideoSource>(client, video_options);
    auto audio_source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);

    auto provider = std::make_shared<MediaTrackProvider>(video_source, audio_source);
    auto switchboard = std::make_shared<Switchboard>(provider, [client](DataChannelLabel label) {
        return CreateGrpcInputSenderForLabel(client, label);
    });

    RtcService rtc_service(switchboard);
    ::goldfish::grpc::v2::RtcServiceImpl v2_rtc_service(switchboard);

    const std::string server_address = absl::GetFlag(FLAGS_grpc_address);
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, BuildCredentials());
    builder.RegisterService(&rtc_service);
    builder.RegisterService(&v2_rtc_service);

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
    if (absl::GetFlag(FLAGS_list_codecs)) {
        goldfish::videobridge::ListCodecs();
        return 0;
    }
    return goldfish::videobridge::RunServer();
}
