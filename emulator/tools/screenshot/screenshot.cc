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
#include <cerrno>
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
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
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
using ::android::emulation::control::GrpcStatusToAbslStatus;
using ::android::emulation::control::Image;
using ::android::emulation::control::ImageFormat;

std::atomic<bool> g_stop{false};

void HandleSignal(int /*signal*/) {
    g_stop = true;
}

absl::Status FileIoErrorToStatus(int err, absl::string_view message) {
    if (err != 0) {
        return absl::ErrnoToStatus(err, message);
    }
    return absl::InternalError(message);
}

absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>> CreateClient(
        const std::string& address, const std::string& token, const std::string& discovery_file) {
    return EmulatorGrpcClientBuilder()
            .WithTargetOrDiscovery(address, token, discovery_file)
            .BuildBlocking();
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
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Could not determine screenshot format from output filename '%s'. Use a .png, "
                    ".rgb, .rgb888, .rgba, or .rgba8888 extension, or pass "
                    "--format=png|rgb888|rgba8888.",
                    filename));
        }
    }

    if (fmt == "png") return ImageFormat::PNG;
    if (fmt == "rgb888") return ImageFormat::RGB888;
    if (fmt == "rgba8888") return ImageFormat::RGBA8888;
    return absl::InvalidArgumentError(absl::StrFormat(
            "Invalid --format value '%s'; must be 'auto', 'png', 'rgb888', or 'rgba8888'.", fmt));
}

absl::Status WriteImage(const std::string& filename, const Image& image) {
    errno = 0;
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) {
        const int err = errno;
        return FileIoErrorToStatus(
                err, absl::StrFormat("Failed to open screenshot output file '%s' for writing. "
                                     "Check that the parent directory exists and is writable",
                                     filename));
    }
    errno = 0;
    out.write(image.image().data(), static_cast<std::streamsize>(image.image().size()));
    out.flush();
    if (!out.good()) {
        const int err = errno;
        return FileIoErrorToStatus(
                err, absl::StrFormat("Failed to write %zu-byte screenshot to '%s'. Check available "
                                     "disk space",
                                     image.image().size(), filename));
    }
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

    auto format = GetFormat(absl::GetFlag(FLAGS_format), output);
    if (!format.ok()) {
        LOG(ERROR) << format.status();
        return 1;
    }

    auto client = CreateClient(absl::GetFlag(FLAGS_grpc_address), absl::GetFlag(FLAGS_grpc_token),
                               absl::GetFlag(FLAGS_discovery_file));
    if (!client.ok()) {
        LOG(ERROR) << "Failed to create gRPC client: " << client.status();
        return 1;
    }

    if (auto status = (*client)->Connect(absl::Seconds(5)); !status.ok()) {
        LOG(ERROR) << "Failed to connect to emulator: " << status;
        return 1;
    }

    auto stub = (*client)->Stub<EmulatorController>();
    if (!stub.ok()) {
        LOG(ERROR) << "Failed to create EmulatorController stub: " << stub.status();
        return 1;
    }

    ImageFormat request;
    request.set_format(*format);
    request.set_display(absl::GetFlag(FLAGS_display));

    auto context = (*client)->NewContext();
    if (!context.ok()) {
        LOG(ERROR) << "Failed to create gRPC client context: " << context.status();
        return 1;
    }

    const std::string target = (*client)->GetEndpoint().target();
    if (stream) {
        LOG(INFO) << "Starting stream. Press Ctrl+C to stop.";
        auto reader = (*stub)->streamScreenshot((*context).get(), request);
        if (!reader) {
            LOG(ERROR) << "Failed to start streamScreenshot RPC to '" << target << "' (display "
                       << request.display()
                       << "). Check that the emulator gRPC service is running and supports "
                          "EmulatorController.streamScreenshot.";
            return 1;
        }
        Image reply;
        uint32_t count = 0;
        uint32_t max_frames = absl::GetFlag(FLAGS_max_frames);
        absl::Status stream_error = absl::OkStatus();

        auto start_receive = absl::Now();
        while (!g_stop && reader->Read(&reply)) {
            auto end_receive = absl::Now();
            std::string filename = output;
            size_t pos = filename.find("%d");
            if (pos != std::string::npos) {
                filename.replace(pos, 2, std::to_string(count));
            }
            auto start_write = absl::Now();
            stream_error = WriteImage(filename, reply);
            if (!stream_error.ok()) {
                LOG(ERROR) << stream_error;
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
        (*context)->TryCancel();  // Ensure the server stream is cancelled
        auto finish_status = reader->Finish();
        if (!finish_status.ok() && finish_status.error_code() != grpc::StatusCode::CANCELLED) {
            absl::Status rpc_status = GrpcStatusToAbslStatus(finish_status);
            LOG(ERROR) << "streamScreenshot RPC to '" << target << "' (display "
                       << request.display() << ") ended with error: " << rpc_status;
            if (stream_error.ok()) {
                stream_error = rpc_status;
            }
        }
        if (!stream_error.ok()) {
            return 1;
        }
    } else {
        Image reply;
        auto start_receive = absl::Now();
        auto status = (*stub)->getScreenshot((*context).get(), request, &reply);
        if (!status.ok()) {
            LOG(ERROR) << "Failed to capture screenshot from display " << request.display()
                       << " at '" << target << "': " << GrpcStatusToAbslStatus(status);
            return 1;
        }
        auto end_receive = absl::Now();

        auto start_write = absl::Now();
        absl::Status write_status = WriteImage(output, reply);
        if (!write_status.ok()) {
            LOG(ERROR) << write_status;
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
