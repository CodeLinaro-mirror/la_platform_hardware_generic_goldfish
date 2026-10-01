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

#include "android/emulation/control/incubating/screen_recording_impl.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"
#include "goldfish/perfetto/perfetto_categories.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

ScreenRecordingServiceImpl::ScreenRecordingServiceImpl(::goldfish::display::IMultiDisplay* display)
        : display_(*display) {}

Status ScreenRecordingServiceImpl::StartRecording(ServerContext* context,
                                                  const RecordingInfo* request,
                                                  RecordingInfo* response) {
    absl::MutexLock lock(mu_);

    if (recorder_) {
        return Status(grpc::StatusCode::ALREADY_EXISTS, "Recording already in progress.");
    }

    // 1. Prepare and apply defaults
    RecordingInfo configured_request = *request;
    if (configured_request.fps() == 0) configured_request.set_fps(kFPS);
    if (configured_request.bit_rate() == 0) configured_request.set_bit_rate(kDefaultVideoBitrate);
    if (configured_request.time_limit() == 0) configured_request.set_time_limit(kDefaultTimeLimit);

    // 2. Validate Input
    std::string error_msg;
    if (!ValidateRecordingParams(&configured_request, error_msg)) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT, error_msg);
    }

    // 2b. Resolve to absolute path to return the complete path in response.
    auto abs_path_status = android::base::file::make_absolute(configured_request.file_name());
    if (!abs_path_status.ok()) {
        return Status(grpc::StatusCode::INTERNAL, "Failed to resolve absolute path.");
    }
    configured_request.set_file_name(abs_path_status->string());

    auto screen = display_.GetDisplay(configured_request.display());
    if (!screen.ok()) {
        LOG(WARNING) << "Unable to retrieve display: " << screen.status();
        return Status(grpc::StatusCode::UNAVAILABLE, "Display is no longer active.");
    }
    auto display = screen->lock();
    if (!display) {
        return Status(grpc::StatusCode::UNAVAILABLE, "Display is no longer active.");
    }

    auto dims = display->GetDimensions();

    // 3. Check if already recording to this file
    auto it = recordings_.find(configured_request.file_name());
    if (it != recordings_.end()) {
        auto& existing = it->second;
        if (existing.state() == RecordingInfo::RECORDER_STATE_RECORDING) {
            return Status(grpc::StatusCode::ALREADY_EXISTS,
                          "Recording already exists for this file.");
        }
    }

    // If a previous recording file already exists on disk, remove it first so that
    // subsequent recordings to the same staging path (e.g. in Studio or Extended Controls)
    // succeed while still creating the new file exclusively with restricted permissions.
    if (android::base::file::exists(configured_request.file_name())) {
        auto rm_status = android::base::file::rm(configured_request.file_name());
        if (!rm_status.ok()) {
            LOG(WARNING) << "Unable to remove existing recording file: " << rm_status;
            return Status(grpc::StatusCode::INTERNAL,
                          absl::StrCat("Unable to remove existing recording file: ",
                                       rm_status.message()));
        }
    }

    // Create the file exclusively to prevent TOCTOU race and ensure restricted permissions.
    auto file_status =
            android::base::file::CreatePrivateFileExclusive(configured_request.file_name(), "");
    if (!file_status.ok()) {
        if (file_status.code() == absl::StatusCode::kAlreadyExists) {
            return Status(grpc::StatusCode::ALREADY_EXISTS, "File already exists on disk.");
        }
        LOG(ERROR) << "Failed to create file exclusively: " << file_status;
        return Status(grpc::StatusCode::INTERNAL,
                      absl::StrCat("Failed to create recording file: ", file_status.message()));
    }

    // 4. Start the Recorder
    const int kFps = configured_request.fps();
    const int kBitRate = configured_request.bit_rate();
    const int kSeconds = configured_request.time_limit();

    recording_listener_ = display->AddFrameListener([](const ::goldfish::display::FrameEvent&) {});
    if (!display->AcquireLatestFrame().frame) {
        (void)display->WaitForFreshFrame(absl::Now() + absl::Milliseconds(500));
    }

    // We just ignore the width and height from request
    const int width2 = dims.width;
    const int height2 = dims.height;
    recorder_.reset(
            new ::goldfish::display::VideoRecorder(width2, height2, kFps, kBitRate, kSeconds));
    const char* kFileName = configured_request.file_name().c_str();
    struct FrameRepeatState {
        ::goldfish::display::FrameId last_frame_id = 0;
        std::vector<uint8_t> last_encoded;
    };
    auto repeat_state = std::make_shared<FrameRepeatState>();

    auto frame_generator = [display, repeat_state](uint8_t* pixels, int w, int h, int frameIndex,
                                                   double time) {
        TRACE_EVENT("display", "ScreenRecording::GenerateFrame", "frame_index", frameIndex, "time",
                    time);
        if (pixels == nullptr || w <= 0 || h <= 0) {
            return;
        }
        const size_t dst_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
        auto latest = display->AcquireLatestFrame();
        if (!latest.frame) {
            if (repeat_state->last_encoded.size() == dst_bytes) {
                std::memcpy(pixels, repeat_state->last_encoded.data(), dst_bytes);
            } else {
                std::memset(pixels, 0, dst_bytes);
            }
            return;
        }

        const auto frame_id = latest.frame->event().frame_id;
        if (frame_id == repeat_state->last_frame_id &&
            repeat_state->last_encoded.size() == dst_bytes) {
            std::memcpy(pixels, repeat_state->last_encoded.data(), dst_bytes);
            return;
        }

        const auto f_dims = latest.frame->dimensions();
        if (f_dims.width == static_cast<uint32_t>(w) && f_dims.height == static_cast<uint32_t>(h)) {
            const ::goldfish::display::ReadRequest req{
                .rotation = ::goldfish::display::ImageRotation::kRotation0,
                .output = {0, 0},
            };
            const ::goldfish::display::Rgb888Target target{
                .plane =
                        ::goldfish::display::Plane{
                            .data = absl::MakeSpan(pixels, dst_bytes),
                            .stride = static_cast<size_t>(w) * 3,
                        },
            };
            if (latest.frame->ReadInto(req, target).ok()) {
                repeat_state->last_frame_id = frame_id;
                repeat_state->last_encoded.assign(pixels, pixels + dst_bytes);
                TRACE_EVENT_INSTANT("display", "ScreenRecording::FrameGenerated",
                                    perfetto::Flow::ProcessScoped(frame_id), "seq", frame_id,
                                    "frame_index", frameIndex);
            } else if (repeat_state->last_encoded.size() == dst_bytes) {
                std::memcpy(pixels, repeat_state->last_encoded.data(), dst_bytes);
            } else {
                std::memset(pixels, 0, dst_bytes);
            }
            return;
        }

        std::memset(pixels, 0, dst_bytes);
        const uint32_t fit_w = std::min<uint32_t>(f_dims.width, static_cast<uint32_t>(w));
        const uint32_t fit_h = std::min<uint32_t>(f_dims.height, static_cast<uint32_t>(h));
        if (fit_w > 0 && fit_h > 0) {
            const ::goldfish::display::ReadRequest req{
                .rotation = ::goldfish::display::ImageRotation::kRotation0,
                .output = {fit_w, fit_h},
            };
            std::vector<uint8_t> tmp;
            if (latest.frame
                        ->ReadInto(req,
                                   [&](const ::goldfish::display::ReadLayout& layout)
                                           -> absl::StatusOr<::goldfish::display::Rgb888Target> {
                                       tmp.resize(layout.total_bytes);
                                       return ::goldfish::display::Rgb888Target{
                                           .plane = {.data = absl::MakeSpan(tmp)},
                                       };
                                   })
                        .ok()) {
                const uint32_t off_x = (static_cast<uint32_t>(w) - fit_w) / 2;
                const uint32_t off_y = (static_cast<uint32_t>(h) - fit_h) / 2;
                for (uint32_t row = 0; row < fit_h; ++row) {
                    std::memcpy(pixels + ((off_y + row) * static_cast<size_t>(w) + off_x) * 3,
                                tmp.data() + row * static_cast<size_t>(fit_w) * 3,
                                static_cast<size_t>(fit_w) * 3);
                }
                repeat_state->last_frame_id = frame_id;
                repeat_state->last_encoded.assign(pixels, pixels + dst_bytes);
                TRACE_EVENT_INSTANT("display", "ScreenRecording::FrameGenerated",
                                    perfetto::Flow::ProcessScoped(frame_id), "seq", frame_id,
                                    "frame_index", frameIndex);
            } else if (repeat_state->last_encoded.size() == dst_bytes) {
                std::memcpy(pixels, repeat_state->last_encoded.data(), dst_bytes);
            }
        }
    };
    if (!recorder_->Start(kFileName, frame_generator)) {
        recorder_.reset();
        recording_listener_.reset();
        return Status(grpc::StatusCode::INTERNAL,
                      "Failed to initialize video encoder. Check emulator logs for details.");
    }

    // 5. Update State
    configured_request.set_state(RecordingInfo::RECORDER_STATE_RECORDING);

    // Store it
    recordings_[configured_request.file_name()] = configured_request;

    // 6. Prepare Response
    *response = configured_request;

    LOG(INFO) << "Started screen recording. File: " << configured_request.file_name()
              << ", Display: " << configured_request.display()
              << ", FPS: " << configured_request.fps()
              << ", Bitrate: " << configured_request.bit_rate();
    return Status::OK;
}

Status ScreenRecordingServiceImpl::StopRecording(ServerContext* context,
                                                 const RecordingInfo* request,
                                                 RecordingInfo* response) {
    absl::MutexLock lock(mu_);

    if (!recorder_) {
        return Status(grpc::StatusCode::NOT_FOUND, "Recording not started.");
    }

    auto abs_path_status = android::base::file::make_absolute(request->file_name());
    if (!abs_path_status.ok()) {
        return Status(grpc::StatusCode::INTERNAL, "Failed to resolve absolute path.");
    }
    auto it = recordings_.find(abs_path_status->string());
    if (it == recordings_.end()) {
        return Status(grpc::StatusCode::NOT_FOUND, "Recording not found.");
    }

    // Update state to STOPPED
    it->second.set_state(RecordingInfo::RECORDER_STATE_STOPPED);
    *response = it->second;

    recorder_->Stop();
    recorder_.reset();
    recording_listener_.reset();

    LOG(INFO) << "Stopped screen recording for file: " << request->file_name();
    return Status::OK;
}

Status ScreenRecordingServiceImpl::ListRecordings(ServerContext* context,
                                                  const RecordingInfo* request,
                                                  RecordingInfoList* response) {
    absl::MutexLock lock(mu_);

    for (const auto& pair : recordings_) {
        // We add the recording to the repeated field list
        RecordingInfo* info = response->add_recordings();
        *info = pair.second;
    }

    return Status::OK;
}

Status ScreenRecordingServiceImpl::ReceiveRecordingEvents(ServerContext* context,
                                                          const google::protobuf::Empty* request,
                                                          ServerWriter<RecordingInfo>* writer) {
    // We just stream the current status of all recordings
    // once and then exit.

    absl::MutexLock lock(mu_);

    for (const auto& pair : recordings_) {
        // Write to the stream
        if (!writer->Write(pair.second)) {
            // Stream closed or error
            return Status::CANCELLED;
        }
    }

    return Status::OK;
}

bool ScreenRecordingServiceImpl::ValidateRecordingParams(const RecordingInfo* info,
                                                         std::string& error_msg) {
    // Check file extension
    std::string fname = info->file_name();
    if (fname.length() < 5 || fname.substr(fname.length() - 5) != ".webm") {
        error_msg = "File name must end with .webm";
        return false;
    }

    // Check FPS range
    if (info->fps() < 1 || info->fps() > kMaxFPS) {
        error_msg = absl::StrCat("FPS must be between 1 and ", kMaxFPS);
        return false;
    }

    // Check Bitrate (100k - 25M)
    if (info->bit_rate() < kMinVideoBitrate || info->bit_rate() > kMaxVideoBitrate) {
        error_msg = absl::StrCat("Bitrate must be between ", kMinVideoBitrate, " and ",
                                 kMaxVideoBitrate);
        return false;
    }

    // Check Time Limit range
    if (info->time_limit() < 1 || info->time_limit() > kMaxTimeLimit) {
        error_msg = absl::StrCat("Time limit must be between 1 and ", kMaxTimeLimit);
        return false;
    }

    return true;
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
