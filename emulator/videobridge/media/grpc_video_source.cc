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
#include "grpc_video_source.h"

#include <utility>

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/video/video_frame.h"
#include "libyuv/convert.h"            // NOLINT(misc-header-include-cycle)
#include "libyuv/convert_from_argb.h"  // NOLINT(misc-header-include-cycle)
#include "libyuv/video_common.h"       // NOLINT(misc-header-include-cycle)
#include "rtc_base/time_utils.h"
#pragma clang diagnostic pop

#include "absl/log/log.h"

#include "goldfish/memory/shared_memory.h"

namespace goldfish::videobridge {

using ::android::emulation::control::Image;
using ::android::emulation::control::ImageFormat;
using ::android::emulation::control::ImageTransport;

android::emulation::control::ImageFormat_ImgFormat RgbaToNv12Pipeline::GetGrpcFormat() const {
    return android::emulation::control::ImageFormat::RGBA8888;
}

size_t RgbaToNv12Pipeline::GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const {
    const size_t w = (width > 0) ? width : 3840;
    const size_t h = (height > 0) ? height : 2160;
    return w * h * 4;
}

std::optional<::webrtc::VideoFrame> RgbaToNv12Pipeline::Convert(const uint8_t* raw_data,
                                                                size_t raw_size, uint32_t width,
                                                                uint32_t height,
                                                                int64_t timestamp_us) {
    // VideoToolbox (and H.264/YUV420 in general) requires even dimensions.
    const uint32_t even_width = width & ~1U;
    const uint32_t even_height = height & ~1U;

    if (even_width == 0 || even_height == 0) {
        return std::nullopt;
    }

    auto buffer = ::webrtc::NV12Buffer::Create(static_cast<int>(even_width),
                                               static_cast<int>(even_height));

    const int stride_abgr = static_cast<int>(width * 4);
    const int status =
            libyuv::ABGRToNV12(raw_data, stride_abgr, buffer->MutableDataY(), buffer->StrideY(),
                               buffer->MutableDataUV(), buffer->StrideUV(),
                               static_cast<int>(even_width), static_cast<int>(even_height));

    if (status != 0) {
        return std::nullopt;
    }

    return ::webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_rtp(0)
            .set_timestamp_us(timestamp_us)
            .set_rotation(::webrtc::kVideoRotation_0)
            .build();
}

android::emulation::control::ImageFormat_ImgFormat RgbaToI420Pipeline::GetGrpcFormat() const {
    return android::emulation::control::ImageFormat::RGBA8888;
}

size_t RgbaToI420Pipeline::GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const {
    const size_t w = (width > 0) ? width : 3840;
    const size_t h = (height > 0) ? height : 2160;
    return w * h * 4;
}

std::optional<::webrtc::VideoFrame> RgbaToI420Pipeline::Convert(const uint8_t* raw_data,
                                                                size_t raw_size, uint32_t width,
                                                                uint32_t height,
                                                                int64_t timestamp_us) {
    const uint32_t even_width = width & ~1U;
    const uint32_t even_height = height & ~1U;

    if (even_width == 0 || even_height == 0) {
        return std::nullopt;
    }

    auto buffer = ::webrtc::I420Buffer::Create(static_cast<int>(even_width),
                                               static_cast<int>(even_height));

    const int status = libyuv::ConvertToI420(
            raw_data, raw_size, buffer->MutableDataY(), buffer->StrideY(), buffer->MutableDataU(),
            buffer->StrideU(), buffer->MutableDataV(), buffer->StrideV(),
            /*crop_x=*/0, /*crop_y=*/0, static_cast<int>(width), static_cast<int>(height),
            static_cast<int>(even_width), static_cast<int>(even_height), libyuv::kRotate0,
            libyuv::FOURCC_ABGR);

    if (status != 0) {
        return std::nullopt;
    }

    return ::webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_rtp(0)
            .set_timestamp_us(timestamp_us)
            .set_rotation(::webrtc::kVideoRotation_0)
            .build();
}

GrpcVideoSource::GrpcVideoSource(std::shared_ptr<EmulatorClient> client,
                                 GrpcVideoSourceOptions options,
                                 std::unique_ptr<VideoFormatPipeline> pipeline)
        : client_(std::move(client))
        , options_(std::move(options))
        , pipeline_(std::move(pipeline)) {}

GrpcVideoSource::~GrpcVideoSource() {
    OnStop();
}

void GrpcVideoSource::OnStart() {
    if (!client_ || !client_->IsConnected()) {
        LOG(ERROR) << "Failed to start screenshot capture: Emulator client is disconnected. "
                   << "Ensure the emulator is running and reachable.";
        return;
    }

    bool expected = false;
    if (capture_running_.compare_exchange_strong(expected, true)) {
        LOG(INFO) << "Starting GrpcVideoSource capture loop for display " << options_.display_id
                  << " (" << options_.width << "x" << options_.height
                  << ") connected to emulator at " << client_->TargetAddress();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        context_ = std::make_unique<::grpc::ClientContext>();
        capture_thread_ = std::thread([this]() { CaptureLoop(); });
    }
}

void GrpcVideoSource::OnStop() {
    bool expected = true;
    if (capture_running_.compare_exchange_strong(expected, false)) {
        VLOG(1) << "Stopping GrpcVideoSource capture loop for display " << options_.display_id
                << " connected to emulator at " << client_->TargetAddress();
        if (context_) {
            context_->TryCancel();
        }
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

bool GrpcVideoSource::SetupSharedMemory(ImageFormat* format) {
    if (!options_.shared_memory_path.has_value()) {
        LOG(ERROR) << "Failed to start screenshot capture: transport is kSharedMemory "
                   << "but shared_memory_path is not specified.";
        return false;
    }

    auto* transport = format->mutable_transport();
    transport->set_channel(ImageTransport::MMAP);

    std::string uri = options_.shared_memory_path->string();
    if (!uri.starts_with("file://")) {
        uri = "file://" + uri;
    }
    transport->set_handle(uri);

    const size_t shm_size = pipeline_->GetRequiredSharedMemorySize(options_.width, options_.height);

    shared_memory_ = std::make_unique<::goldfish::memory::SharedMemory>(uri, shm_size);
    const auto backing_path = shared_memory_->BackingFile();
    if (std::filesystem::exists(backing_path)) {
        VLOG(1) << "Shared memory backing file already exists. Cleaning up before creation: "
                << backing_path;
        std::error_code ec;
        std::filesystem::remove(backing_path, ec);
        if (ec) {
            LOG(WARNING) << "Failed to remove existing shared memory file: " << ec.message();
        }
    }
    const auto status = shared_memory_->Create(std::filesystem::perms::owner_read |
                                               std::filesystem::perms::owner_write);
    if (!status.ok()) {
        LOG(WARNING) << "Failed to create shared memory region: " << status
                     << ". Falling back to socket transport.";
        shared_memory_.reset();
        format->clear_transport();
    } else {
        LOG(INFO) << "Configured shared memory transport at: " << uri;
    }
    return true;
}

void GrpcVideoSource::ProcessIncomingImage(const Image& img) {
    const uint32_t width = img.format().width();
    const uint32_t height = img.format().height();

    VLOG(2) << "Received screenshot frame: " << width << "x" << height
            << ", sequence=" << img.seq();

    const uint8_t* raw_data = nullptr;
    size_t raw_size = 0;

    if (shared_memory_ && shared_memory_->IsMapped()) {
        raw_data = reinterpret_cast<const uint8_t*>(shared_memory_->Get());
        raw_size = pipeline_->GetRequiredSharedMemorySize(width, height);
        VLOG(3) << "Loaded frame from shared memory (address=" << static_cast<const void*>(raw_data)
                << ", size=" << raw_size << " bytes)";
    } else {
        const std::string& buffer_data = img.image();
        if (buffer_data.empty()) {
            VLOG(2) << "Received empty screenshot image buffer.";
            return;
        }
        raw_data = reinterpret_cast<const uint8_t*>(buffer_data.data());
        raw_size = buffer_data.size();
        VLOG(3) << "Loaded frame from gRPC payload (size=" << raw_size << " bytes)";
    }

    auto frame = pipeline_->Convert(raw_data, raw_size, width, height, ::webrtc::TimeMicros());
    if (!frame.has_value()) {
        LOG(WARNING) << "Format conversion failed on display " << options_.display_id
                     << ". Frame dropped.";
        return;
    }

    VLOG(2) << "Delivered video frame to adapted WebRTC sink (timestamp_us="
            << frame->timestamp_us() << " us)";
    OnFrame(*frame);
}

void GrpcVideoSource::CaptureLoop() {
    if (!client_ || !client_->IsConnected()) {
        LOG(ERROR) << "Failed to start screenshot capture: Emulator client is disconnected. "
                   << "Ensure the emulator is running and reachable.";
        capture_running_ = false;
        return;
    }

    ImageFormat format;
    format.set_format(pipeline_->GetGrpcFormat());
    format.set_display(options_.display_id);
    format.set_width(options_.width);
    format.set_height(options_.height);

    if (options_.transport == GrpcVideoSourceOptions::Transport::kSharedMemory) {
        if (!SetupSharedMemory(&format)) {
            capture_running_ = false;
            return;
        }
    }

    auto reader = client_->StreamScreenshot(context_.get(), format);
    if (!reader) {
        LOG(ERROR) << "Failed to open gRPC screenshot stream. Verify network or emulator state.";
        capture_running_ = false;
        return;
    }

    Image img;
    while (capture_running_ && reader->Read(&img)) {
        ProcessIncomingImage(img);
    }
    // TODO(jansene): We could signal the source state to webrtc for this video track

    LOG(INFO) << "GrpcVideoSource capture loop exited for display " << options_.display_id
              << " connected to emulator at " << client_->TargetAddress();
    capture_running_ = false;
}

}  // namespace goldfish::videobridge
