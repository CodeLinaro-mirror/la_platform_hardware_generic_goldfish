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
#pragma once

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "grpcpp/grpcpp.h"

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "media/base/adapted_video_track_source.h"
#pragma clang diagnostic pop

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "absl/synchronization/mutex.h"

#include "goldfish/videobridge/emulator_client.h"

namespace goldfish::memory {
class SharedMemory;
}  // namespace goldfish::memory

namespace goldfish::videobridge {

/**
 * @struct GrpcVideoSourceOptions
 * @brief Configuration parameters for setting up GrpcVideoSource.
 */
struct GrpcVideoSourceOptions {
    enum class Transport : std::uint8_t {
        kGrpcBytes,    ///< Stream full frame pixels over the gRPC network.
        kSharedMemory  ///< Read frames directly from a local shared memory region.
    };

    uint32_t display_id = 0;

    // The desired width and height of the video track.
    // NOTE: Providing width = 0 and height = 0 will preserve and stream the real,
    // native device dimensions. This is the optimal emulator path.
    uint32_t width = 0;
    uint32_t height = 0;

    Transport transport = Transport::kGrpcBytes;

    // Mandatory path when transport is kSharedMemory.
    std::optional<std::filesystem::path> shared_memory_path = std::nullopt;
};

/**
 * @class VideoFormatPipeline
 * @brief Strategy defining how to configure and convert emulator screenshots to WebRTC frames.
 *
 * This abstraction decouples the specific image format representation (e.g. RGBA8888, RGB888,
 * NV12, or Native textures) from the capture loop logic in GrpcVideoSource.
 */
class VideoFormatPipeline {
  public:
    virtual ~VideoFormatPipeline() = default;

    /**
     * @brief The gRPC image format enum to request from the emulator.
     * @return The ImageFormat_ImgFormat enum value (e.g. RGBA8888).
     */
    virtual android::emulation::control::ImageFormat_ImgFormat GetGrpcFormat() const = 0;

    /**
     * @brief Calculates the exact shared memory size required to transfer a frame of the given
     * dimensions.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @return The required memory size in bytes. For native textures, this represents the metadata
     * structure size.
     */
    virtual size_t GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const = 0;

    /**
     * @brief Converts the raw incoming bytes/metadata to a WebRTC VideoFrame.
     * @param raw_data Pointer to the raw pixel array or native handle metadata.
     * @param raw_size Size of the raw data in bytes.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param timestamp_us The frame presentation timestamp in microseconds.
     * @return A WebRTC VideoFrame if conversion succeeds, or std::nullopt.
     */
    virtual std::optional<::webrtc::VideoFrame> Convert(const uint8_t* raw_data, size_t raw_size,
                                                        uint32_t width, uint32_t height,
                                                        int64_t timestamp_us) = 0;
};

/**
 * @class RgbaToI420Pipeline
 * @brief Default pipeline requesting RGBA8888 from the emulator and converting it to I420.
 */
class RgbaToI420Pipeline : public VideoFormatPipeline {
  public:
    android::emulation::control::ImageFormat_ImgFormat GetGrpcFormat() const override;
    size_t GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const override;
    std::optional<::webrtc::VideoFrame> Convert(const uint8_t* raw_data, size_t raw_size,
                                                uint32_t width, uint32_t height,
                                                int64_t timestamp_us) override;

  private:
    ::webrtc::scoped_refptr<::webrtc::I420Buffer> buffer_;
};

// TODO(jansene): We should see if we can use RGB888 (less data but it looks like R<->B are swapped)
// TODO(jansene): We should have an NV12 pipeline (way less data to go around)
// TODO(jansene): We should have a kNative pipeline where we use the vulkan buffer directly

/**
 * @class GrpcVideoSource
 * @brief Custom AdaptedVideoTrackSource streaming virtual display screenshots via gRPC.
 *
 * GrpcVideoSource establishes a screenshot subscription stream with the emulator, converts
 * incoming RGB888 buffers to I420 format via libyuv, and pushes frames to registered sinks.
 */
class GrpcVideoSource : public ::webrtc::AdaptedVideoTrackSource {
  public:
    GrpcVideoSource(
            std::shared_ptr<EmulatorClient> client, GrpcVideoSourceOptions options,
            std::unique_ptr<VideoFormatPipeline> pipeline = std::make_unique<RgbaToI420Pipeline>());
    ~GrpcVideoSource() override;

    // AdaptedVideoTrackSource overrides.
    bool is_screencast() const override { return true; }
    absl::optional<bool> needs_denoising() const override { return false; }
    ::webrtc::MediaSourceInterface::SourceState state() const override {
        return ::webrtc::MediaSourceInterface::SourceState::kLive;
    }
    bool remote() const override { return false; }

    /**
     * @brief Spins up the background capture thread.
     */
    void Start();

    /**
     * @brief Cancels the stream and joins the background capture thread.
     */
    void Stop();

  private:
    void CaptureLoop();
    bool SetupSharedMemory(android::emulation::control::ImageFormat* format);
    void ProcessIncomingImage(const android::emulation::control::Image& img);

    std::shared_ptr<EmulatorClient> client_;
    GrpcVideoSourceOptions options_;

    std::atomic<bool> running_{false};
    std::thread capture_thread_;
    ::grpc::ClientContext context_;

    std::unique_ptr<::goldfish::memory::SharedMemory> shared_memory_;
    std::unique_ptr<VideoFormatPipeline> pipeline_;
};

}  // namespace goldfish::videobridge
