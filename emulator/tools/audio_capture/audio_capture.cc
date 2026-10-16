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
#include <iomanip>
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
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/emulator_grpc_client.h"
#include "emulator_controller.grpc.pb.h"
#include "wav_writer.h"

ABSL_FLAG(std::string, discovery_file, "",
          "Path to the emulator discovery file. If empty, the first discovered "
          "emulator will be used.");
ABSL_FLAG(std::string, grpc_address, "",
          "Direct address and port of the emulator gRPC service (e.g. localhost:8554).");
ABSL_FLAG(std::string, grpc_token, "", "Authentication token for the emulator gRPC service.");
ABSL_FLAG(std::string, output, "capture.wav", "Output path for the captured WAV audio file.");
ABSL_FLAG(uint32_t, sample_rate, 48000, "Audio sample rate in Hz (default: 48000).");
ABSL_FLAG(std::string, channels, "stereo", "Audio channel configuration ('mono' or 'stereo').");
ABSL_FLAG(double, duration_sec, 10.0,
          "Duration in seconds to record (default: 10.0s). Set to 0 to record until Ctrl+C.");

namespace {

using ::android::emulation::control::AudioFormat;
using ::android::emulation::control::AudioPacket;
using ::android::emulation::control::BlockingEmulatorGrpcClient;
using ::android::emulation::control::EmulatorController;
using ::android::emulation::control::EmulatorGrpcClientBuilder;
using ::android::emulation::control::GrpcStatusToAbslStatus;
using ::android::emulation::tools::WavWriter;

std::atomic<bool> g_stop{false};

void HandleSignal(int /*signal*/) {
    g_stop = true;
}

absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>> CreateClient(
        const std::string& address, const std::string& token, const std::string& discovery_file) {
    auto client = EmulatorGrpcClientBuilder()
                          .WithTargetOrDiscovery(address, token, discovery_file)
                          .BuildBlocking();
    if (!client.ok()) {
        return client.status();
    }

    LOG(INFO) << "Connecting to emulator at " << (*client)->GetEndpoint().target() << "...";
    if (auto status = (*client)->Connect(absl::Seconds(5)); !status.ok()) {
        return status;
    }
    LOG(INFO) << "Successfully connected to emulator.";
    return client;
}

absl::Status CaptureAudio(BlockingEmulatorGrpcClient& client, const std::string& output_path,
                          uint32_t sample_rate, const std::string& channels, double duration_sec) {
    if (sample_rate == 0) {
        return absl::InvalidArgumentError(
                "Invalid --sample_rate value 0; must be greater than 0 Hz (e.g. 44100 or 48000).");
    }
    if (channels != "mono" && channels != "stereo") {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Invalid --channels value '%s'; must be 'mono' or 'stereo'.", channels));
    }
    if (duration_sec < 0.0) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Invalid --duration_sec value %g; must be >= 0 (use 0 to record until Ctrl+C).",
                duration_sec));
    }

    auto stub = client.Stub<EmulatorController>();
    if (!stub.ok()) {
        return stub.status();
    }

    auto context = client.NewContext();
    if (!context.ok()) {
        return context.status();
    }

    if (duration_sec > 0.0) {
        // Set a deadline on the ClientContext to handle a stuck or silent server where
        // reader->Read() would otherwise block indefinitely if no audio packets are produced.
        // We add a 1-second grace period so normal recording loops can exit cleanly and cancel.
        (*context)->set_deadline(
                absl::ToChronoTime(absl::Now() + absl::Seconds(duration_sec + 1.0)));
    }

    const uint16_t num_channels = (channels == "mono") ? 1 : 2;
    AudioFormat request;
    request.set_samplingrate(sample_rate);
    request.set_channels(num_channels == 1 ? AudioFormat::Mono : AudioFormat::Stereo);
    request.set_format(AudioFormat::AUD_FMT_S16);

    WavWriter wav_writer;
    if (auto status = wav_writer.Open(output_path, sample_rate, num_channels, 16); !status.ok()) {
        return status;
    }

    LOG(INFO) << "Initiating streamAudio RPC (" << sample_rate << " Hz, " << channels << ")...";

    auto reader = (*stub)->streamAudio((*context).get(), request);
    if (!reader) {
        return absl::UnavailableError(absl::StrFormat(
                "Failed to start streamAudio RPC to '%s' (%u Hz, %s). Check that the emulator "
                "gRPC service is running and supports EmulatorController.streamAudio.",
                client.GetEndpoint().target(), sample_rate, channels));
    }

    if (duration_sec > 0.0) {
        LOG(INFO) << "Recording for " << duration_sec << " seconds to " << output_path
                  << " (press Ctrl+C to stop early)...";
    } else {
        LOG(INFO) << "Recording continuously to " << output_path << " (press Ctrl+C to stop)...";
    }

    const absl::Time start_time = absl::Now();
    const absl::Duration target_duration = absl::Seconds(duration_sec);
    size_t packet_count = 0;
    absl::Status write_status = absl::OkStatus();

    AudioPacket packet;
    while (!g_stop && reader->Read(&packet)) {
        packet_count++;

        if (packet_count == 1) {
            LOG(INFO) << "Received first audio packet: " << packet.audio().size() << " bytes ("
                      << packet.format().samplingrate() << " Hz, "
                      << (packet.format().channels() == AudioFormat::Mono ? "Mono" : "Stereo")
                      << ").";
        }

        if (!packet.audio().empty()) {
            write_status = wav_writer.Write(packet.audio().data(), packet.audio().size());
            if (!write_status.ok()) {
                LOG(ERROR) << "Failed to write audio packet #" << packet_count << ": "
                           << write_status;
                break;
            }
        }

        LOG_EVERY_N_SEC(INFO, 1) << "Captured " << (absl::Now() - start_time) << " | "
                                 << packet_count << " packets | " << std::fixed
                                 << std::setprecision(1) << (wav_writer.bytes_written() / 1024.0)
                                 << " KB";

        if (duration_sec > 0.0 && absl::Now() - start_time >= target_duration) {
            LOG(INFO) << "Requested duration reached (" << (absl::Now() - start_time)
                      << "). Stopping capture.";
            break;
        }
    }

    (*context)->TryCancel();
    wav_writer.Close();

    const absl::Duration total_elapsed = absl::Now() - start_time;

    auto finish_status = reader->Finish();
    if (!finish_status.ok() && finish_status.error_code() != grpc::StatusCode::CANCELLED) {
        LOG(WARNING) << "streamAudio RPC to '" << client.GetEndpoint().target()
                     << "' ended with error: " << GrpcStatusToAbslStatus(finish_status);
    }

    if (!write_status.ok()) {
        return write_status;
    }

    if (packet_count == 0) {
        LOG(WARNING) << "No audio packets were received from the emulator (guest audio might be "
                        "idle, muted, or unsupported).";
    }

    LOG(INFO) << "Capture finished: wrote " << wav_writer.bytes_written() << " bytes ("
              << packet_count << " packets in " << total_elapsed << ") to " << output_path;

    return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage(
            "Usage: audio_capture [options]\nRecords audio from a running Android Emulator to a "
            "WAV "
            "file.");
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    auto client = CreateClient(absl::GetFlag(FLAGS_grpc_address), absl::GetFlag(FLAGS_grpc_token),
                               absl::GetFlag(FLAGS_discovery_file));
    if (!client.ok()) {
        LOG(ERROR) << "Failed to connect to emulator: " << client.status();
        return 1;
    }

    auto status =
            CaptureAudio(**client, absl::GetFlag(FLAGS_output), absl::GetFlag(FLAGS_sample_rate),
                         absl::GetFlag(FLAGS_channels), absl::GetFlag(FLAGS_duration_sec));
    if (!status.ok()) {
        LOG(ERROR) << "Audio capture failed: " << status;
        return 1;
    }

    return 0;
}
