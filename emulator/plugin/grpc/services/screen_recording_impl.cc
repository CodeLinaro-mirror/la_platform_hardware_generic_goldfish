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

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"

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

    // We just ignore the width and height from request
    const int width2 = dims.width;
    const int height2 = dims.height;
    recorder_.reset(
            new ::goldfish::display::VideoRecorder(width2, height2, kFps, kBitRate, kSeconds));
    const char* kFileName = configured_request.file_name().c_str();
    auto frame_generator = [display, width2, height2](uint8_t* pixels, int w, int h, int frameIndex,
                                                      double time) {
        auto format2 = ::goldfish::display::PixelFormat::kRgb888;
        auto rotation2 = ::goldfish::display::ImageRotation::kRotation0;
        size_t c_pixels = static_cast<size_t>(width2) * height2 * 3;
        auto seq = display->GetPixels(format2, width2, height2, rotation2, pixels, &c_pixels);
    };
    if (!recorder_->Start(kFileName, frame_generator)) {
        recorder_.reset();
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
