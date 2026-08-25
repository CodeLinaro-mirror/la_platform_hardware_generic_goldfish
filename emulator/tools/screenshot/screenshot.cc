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

#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "emulator_controller.grpc.pb.h"

ABSL_FLAG(std::string, discovery_file, "",
          "Path to the emulator discovery file. If empty, the first discovered "
          "emulator will be used.");
ABSL_FLAG(std::string, grpc_address, "",
          "gRPC address of the emulator (e.g., localhost:8554). If specified, "
          "it overrides discovery_file.");
ABSL_FLAG(std::string, grpc_token, "", "Authentication token for gRPC, if required.");
ABSL_FLAG(std::string, output, "screenshot.png",
          "Output filename. If --stream is used, must contain %d (default: screenshot_%d.png). "
          "The extension determines the format if --format is auto.");
ABSL_FLAG(std::string, format, "auto",
          "Format of the screenshot (auto, png, rgb888, rgba8888). 'auto' determines from output "
          "extension.");
ABSL_FLAG(uint32_t, display, 0, "Display ID to capture (default: 0).");
ABSL_FLAG(bool, stream, false, "Stream screenshots continuously.");
ABSL_FLAG(uint32_t, max_frames, 0,
          "Maximum number of frames to capture if --stream is used (0 = infinite).");

namespace {

using ::android::emulation::control::BlockingEmulatorGrpcClient;
using ::android::emulation::control::EmulatorController;
using ::android::emulation::control::EmulatorGrpcClientBuilder;
using ::android::emulation::control::Image;
using ::android::emulation::control::ImageFormat;
using ::android::emulation::remote::Endpoint;

std::atomic<bool> g_stop{false};

void HandleSignal(int /*signal*/) {
    g_stop = true;
}

absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>> CreateClient(
        const std::string& address, const std::string& token, const std::string& discovery_file) {
    EmulatorGrpcClientBuilder builder;

    if (!address.empty()) {
        Endpoint endpoint;
        endpoint.set_target(address);
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
    return client;
}

absl::StatusOr<ImageFormat::ImgFormat> GetFormat(const std::string& format_str,
                                                 const std::string& filename) {
    std::string fmt = format_str;
    if (fmt == "auto") {
        if (absl::EndsWithIgnoreCase(filename, ".png")) {
            fmt = "png";
        } else if (absl::EndsWithIgnoreCase(filename, ".rgb888") ||
                   absl::EndsWithIgnoreCase(filename, ".rgb")) {
            fmt = "rgb888";
        } else if (absl::EndsWithIgnoreCase(filename, ".rgba8888") ||
                   absl::EndsWithIgnoreCase(filename, ".rgba")) {
            fmt = "rgba8888";
        } else {
            return absl::InvalidArgumentError(
                    "Could not determine format from filename extension.");
        }
    }

    if (fmt == "png") return ImageFormat::PNG;
    if (fmt == "rgb888") return ImageFormat::RGB888;
    if (fmt == "rgba8888") return ImageFormat::RGBA8888;
    return absl::InvalidArgumentError(std::string("Unknown format: ") + fmt);
}

absl::Status WriteImage(const std::string& filename, const Image& image) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        return absl::UnavailableError(std::string("Failed to open file for writing: ") + filename);
    }
    out.write(image.image().data(), image.image().size());
    out.close();
    return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage("Capture screenshots from the emulator via gRPC.");
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::string output = absl::GetFlag(FLAGS_output);
    bool stream = absl::GetFlag(FLAGS_stream);

    if (stream && output.find("%d") == std::string::npos) {
        // Automatically inject %d if missing in stream mode
        size_t dot_pos = output.find_last_of('.');
        if (dot_pos != std::string::npos) {
            output.insert(dot_pos, "_%d");
        } else {
            output += "_%d";
        }
        LOG(INFO) << "Output filename adjusted to: " << output;
    }

    auto format_res = GetFormat(absl::GetFlag(FLAGS_format), output);
    if (!format_res.ok()) {
        LOG(ERROR) << format_res.status().message();
        return 1;
    }

    auto client_res =
            CreateClient(absl::GetFlag(FLAGS_grpc_address), absl::GetFlag(FLAGS_grpc_token),
                         absl::GetFlag(FLAGS_discovery_file));
    if (!client_res.ok()) {
        LOG(ERROR) << client_res.status().message();
        return 1;
    }

    auto client = std::move(client_res.value());
    if (auto status = client->Connect(absl::Seconds(5)); !status.ok()) {
        LOG(ERROR) << "Failed to connect: " << status.message();
        return 1;
    }

    auto stub_res = client->Stub<EmulatorController>();
    if (!stub_res.ok()) {
        LOG(ERROR) << "Failed to get stub: " << stub_res.status().message();
        return 1;
    }
    auto stub = std::move(stub_res.value());

    ImageFormat request;
    request.set_format(*format_res);
    request.set_display(absl::GetFlag(FLAGS_display));

    auto context_res = client->NewContext();
    if (!context_res.ok()) {
        LOG(ERROR) << "Failed to create context: " << context_res.status().message();
        return 1;
    }
    auto context = std::move(context_res.value());

    if (stream) {
        LOG(INFO) << "Starting stream. Press Ctrl+C to stop.";
        auto reader = stub->streamScreenshot(context.get(), request);
        Image reply;
        uint32_t count = 0;
        uint32_t max_frames = absl::GetFlag(FLAGS_max_frames);

        auto start_receive = absl::Now();
        while (!g_stop && reader->Read(&reply)) {
            auto end_receive = absl::Now();
            std::string filename = output;
            size_t pos = filename.find("%d");
            if (pos != std::string::npos) {
                filename.replace(pos, 2, std::to_string(count));
            }
            auto start_write = absl::Now();
            absl::Status write_status = WriteImage(filename, reply);
            if (!write_status.ok()) {
                LOG(ERROR) << write_status.message();
                break;
            }
            auto end_write = absl::Now();
            std::cout << "Wrote " << filename << " (" << reply.image().size() << " bytes) | "
                      << "Receive: " << absl::FormatDuration(end_receive - start_receive) << " | "
                      << "Write: " << absl::FormatDuration(end_write - start_write) << "\n";
            count++;

            if (max_frames > 0 && count >= max_frames) {
                break;
            }
            start_receive = absl::Now();
        }
        context->TryCancel();  // Ensure the server stream is cancelled
        auto status = reader->Finish();
        if (!status.ok() && status.error_code() != grpc::StatusCode::CANCELLED) {
            LOG(ERROR) << "Stream finished with error: " << status.error_message();
        }
    } else {
        Image reply;
        auto start_receive = absl::Now();
        auto status = stub->getScreenshot(context.get(), request, &reply);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to get screenshot: " << status.error_message();
            return 1;
        }
        auto end_receive = absl::Now();

        auto start_write = absl::Now();
        absl::Status write_status = WriteImage(output, reply);
        if (!write_status.ok()) {
            LOG(ERROR) << write_status.message();
            return 1;
        }
        auto end_write = absl::Now();
        std::cout << "Wrote " << output << " (" << reply.image().size() << " bytes) | "
                  << "Receive: " << absl::FormatDuration(end_receive - start_receive) << " | "
                  << "Write: " << absl::FormatDuration(end_write - start_write) << "\n";
    }

    LOG(INFO) << "Screenshot capture finished.";
    return 0;
}
