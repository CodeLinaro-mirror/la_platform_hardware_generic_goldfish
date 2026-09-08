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
#include "screen_record_commands.h"

#include <filesystem>
#include <fstream>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/screen_recording_constants.h"
#include "legacy_console_bridge.h"
#include "screen_recording_service.grpc.pb.h"
#include "screen_recording_service.pb.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;
using android::emulation::control::incubating::RecordingInfo;
using android::emulation::control::incubating::ScreenRecording;

using android::emulation::control::kMaxFPS;
using android::emulation::control::kMaxTimeLimit;
using android::emulation::control::kMaxVideoBitrate;
using android::emulation::control::kMinVideoBitrate;

namespace {

absl::Status ParseRecordingInfo(ArgStream& args, RecordingInfo* request) {
    std::string filename;

    while (!args.Empty()) {
        std::string token = args.Peek();
        if (token.starts_with("--")) {
            args.Next();  // consume option
            if (token == "--size") {
                std::string size_str = args.Next();
                std::vector<std::string_view> parts = absl::StrSplit(size_str, 'x');
                if (parts.size() != 2) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Invalid size '", size_str, "', must be width x height"));
                }
                uint32_t width, height;
                if (!absl::SimpleAtoi(parts[0], &width) || !absl::SimpleAtoi(parts[1], &height)) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Invalid size '", size_str, "', must be width x height"));
                }
                if (width == 0 || height == 0) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Invalid size ", width, "x", height,
                                         ", width and height may not be zero"));
                }
                request->set_width(width);
                request->set_height(height);
            } else if (token == "--bit-rate") {
                std::string rate_str = args.Next();
                uint32_t bit_rate;
                if (absl::EndsWith(rate_str, "M")) {
                    std::string_view num_str =
                            std::string_view(rate_str).substr(0, rate_str.size() - 1);
                    if (!absl::SimpleAtoi(num_str, &bit_rate)) {
                        return absl::InvalidArgumentError("Invalid bit-rate value.");
                    }
                    bit_rate *= 1000000;
                } else {
                    if (!absl::SimpleAtoi(rate_str, &bit_rate)) {
                        return absl::InvalidArgumentError("Invalid bit-rate value.");
                    }
                }
                if (bit_rate < kMinVideoBitrate || bit_rate > kMaxVideoBitrate) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Bit rate ", bit_rate, "bps outside acceptable range [",
                                         kMinVideoBitrate, ",", kMaxVideoBitrate, "]"));
                }
                request->set_bit_rate(bit_rate);
            } else if (token == "--time-limit") {
                auto val = args.NextInt();
                if (!val.ok()) return val.status();
                uint32_t time_limit = *val;
                if (time_limit == 0 || time_limit > kMaxTimeLimit) {
                    return absl::InvalidArgumentError(absl::StrCat("Time limit ", time_limit,
                                                                   "s outside acceptable range [1,",
                                                                   kMaxTimeLimit, "]"));
                }
                request->set_time_limit(time_limit);
            } else if (token == "--fps") {
                auto val = args.NextInt();
                if (!val.ok()) return val.status();
                uint32_t fps = *val;
                if (fps == 0 || fps > kMaxFPS) {
                    return absl::InvalidArgumentError(absl::StrCat(
                            "FPS ", fps, "ds outside acceptable range [1,", kMaxFPS, "]"));
                }
                request->set_fps(fps);
            } else if (token == "--display") {
                auto val = args.NextInt();
                if (!val.ok()) return val.status();
                request->set_display(*val);
            } else {
                return absl::InvalidArgumentError(
                        "Invalid arguments (see help screenrecord start).");
            }
        } else {
            filename = args.Next();
            break;  // Assume filename is the last argument
        }
    }

    if (filename.empty()) {
        return absl::InvalidArgumentError("Must provide an output filename");
    }

    request->set_file_name(filename);
    return absl::OkStatus();
}

absl::Status ParseScreenshotArgs(ArgStream& args, uint32_t* display, std::filesystem::path* path) {
    std::string path_str;
    *display = 0;

    while (!args.Empty()) {
        std::string token = args.Peek();
        if (token.starts_with("--")) {
            args.Next();  // consume option
            if (token == "--display") {
                auto val = args.NextInt();
                if (!val.ok()) return val.status();
                *display = *val;
            } else {
                return absl::InvalidArgumentError(
                        "Invalid arguments (see help screenrecord screenshot).");
            }
        } else {
            path_str = args.Next();
            break;
        }
    }

    absl::Time now = absl::Now();
    std::string file_name = absl::StrFormat("Screenshot_%lld.png",
                                            static_cast<long long>(absl::ToUnixSeconds(now)));

    if (absl::EndsWith(path_str, ".png")) {
        *path = path_str;
    } else {
        *path = path_str.empty() ? std::filesystem::path(file_name)
                                 : (std::filesystem::path(path_str) / file_name);
    }
    return absl::OkStatus();
}
}  // namespace

void RegisterScreenRecordCommands(CommandRegistryBuilder::NodeBuilder& screenrecord) {
    screenrecord.On("start", "start screen recording",
                    [](LegacyConsoleBridge::ConsoleContext& ctx, ArgStream& args) -> absl::Status {
                        RecordingInfo request;
                        RETURN_IF_ERROR(ParseRecordingInfo(args, &request));

                        ASSIGN_OR_RETURN(auto stub, ctx.ScreenRecordingStub());
                        ASSIGN_OR_RETURN(auto context,
                                         ctx.NewContext(std::chrono::system_clock::now() +
                                                        std::chrono::seconds(10)));

                        RecordingInfo response;
                        auto status = stub->StartRecording(context.get(), request, &response);
                        if (!status.ok()) {
                            return GrpcStatusToAbslStatus(status);
                        }

                        return absl::OkStatus();
                    });

    screenrecord.On(
            "stop", "stop screen recording",
            [](LegacyConsoleBridge::ConsoleContext& ctx) -> absl::Status {
                ASSIGN_OR_RETURN(auto stub, ctx.ScreenRecordingStub());

                // List recordings
                ASSIGN_OR_RETURN(auto context_list,
                                 ctx.NewContext(std::chrono::system_clock::now() +
                                                std::chrono::seconds(10)));
                RecordingInfo list_request;
                android::emulation::control::incubating::RecordingInfoList list_response;
                auto status =
                        stub->ListRecordings(context_list.get(), list_request, &list_response);
                if (!status.ok()) {
                    return absl::InternalError("Failed to list recordings.");
                }

                // Stop each active recording
                for (const auto& info : list_response.recordings()) {
                    if (info.state() == android::emulation::control::incubating::RecordingInfo::
                                                RECORDER_STATE_RECORDING) {
                        ASSIGN_OR_RETURN(auto context_stop,
                                         ctx.NewContext(std::chrono::system_clock::now() +
                                                        std::chrono::seconds(10)));
                        RecordingInfo stop_response;
                        auto status_stop =
                                stub->StopRecording(context_stop.get(), info, &stop_response);
                        if (!status_stop.ok()) {
                            LOG(WARNING) << "Failed to stop recording: " << info.file_name();
                        }
                    }
                }

                return absl::OkStatus();
            });

    screenrecord.On("screenshot" /* do_screenrecord_screenshot */, "Take a screenshot",
                    "'screenrecord screenshot [options] <dirname>'\r\n"
                    "\r\nTakes a single screenshot of emulator's display "
                    "and saves the resulting PNG in <dirname>.\r\n"
                    "\r\nOptions:\r\n"
                    "  --display ID\r\n"
                    "    Set display to take screenshot. Default is the device's "
                    "main display ID = 0\r\n",
                    [](LegacyConsoleBridge::ConsoleContext& ctx, ArgStream& args) -> absl::Status {
                        uint32_t display;
                        std::filesystem::path path;
                        RETURN_IF_ERROR(ParseScreenshotArgs(args, &display, &path));

                        ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                        ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                        android::emulation::control::ImageFormat request;
                        request.set_format(android::emulation::control::ImageFormat::PNG);
                        request.set_display(display);

                        android::emulation::control::Image response;
                        auto status = stub->getScreenshot(context.get(), request, &response);
                        if (!status.ok()) {
                            return GrpcStatusToAbslStatus(status);
                        }

                        std::ofstream file(path, std::ios::binary);
                        if (!file.is_open()) {
                            return absl::InternalError(absl::StrCat(
                                    "Failed to open file for writing: ", path.string()));
                        }
                        file.write(response.image().data(), response.image().size());
                        file.close();

                        return absl::OkStatus();
                    });
}

}  // namespace goldfish::telnet
