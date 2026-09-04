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
#include "api/video/nv12_buffer.h"
#pragma clang diagnostic pop

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "goldfish/videobridge/emulator_client.h"
#include "goldfish/videobridge/managed_video_track_source.h"

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
 * Strategy defining how to configure and convert emulator screenshots to WebRTC frames.
 */
class VideoFormatPipeline {
  public:
    virtual ~VideoFormatPipeline() = default;

    virtual android::emulation::control::ImageFormat_ImgFormat GetGrpcFormat() const = 0;
    virtual size_t GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const = 0;
    virtual std::optional<::webrtc::VideoFrame> Convert(const uint8_t* raw_data, size_t raw_size,
                                                        uint32_t width, uint32_t height,
                                                        int64_t timestamp_us) = 0;
};

/**
 * Default pipeline requesting RGBA8888 from the emulator and converting it to NV12.
 */
class RgbaToNv12Pipeline : public VideoFormatPipeline {
  public:
    android::emulation::control::ImageFormat_ImgFormat GetGrpcFormat() const override;
    size_t GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const override;
    std::optional<::webrtc::VideoFrame> Convert(const uint8_t* raw_data, size_t raw_size,
                                                uint32_t width, uint32_t height,
                                                int64_t timestamp_us) override;
};

/**
 * Pipeline requesting RGBA8888 from the emulator and converting it to I420.
 */
class RgbaToI420Pipeline : public VideoFormatPipeline {
  public:
    android::emulation::control::ImageFormat_ImgFormat GetGrpcFormat() const override;
    size_t GetRequiredSharedMemorySize(uint32_t width, uint32_t height) const override;
    std::optional<::webrtc::VideoFrame> Convert(const uint8_t* raw_data, size_t raw_size,
                                                uint32_t width, uint32_t height,
                                                int64_t timestamp_us) override;
};

/**
 * WebRTC video track source streaming virtual display screenshots via gRPC.
 *
 * Automatically connects to emulator screenshot streaming when WebRTC sinks are active.
 */
class GrpcVideoSource : public ManagedVideoTrackSource {
  public:
    GrpcVideoSource(
            std::shared_ptr<EmulatorClient> client, GrpcVideoSourceOptions options,
            std::unique_ptr<VideoFormatPipeline> pipeline = std::make_unique<RgbaToNv12Pipeline>());
    ~GrpcVideoSource() override;

  protected:
    void OnStart() override;
    void OnStop() override;

  private:
    void CaptureLoop();
    bool SetupSharedMemory(android::emulation::control::ImageFormat* format);
    void ProcessIncomingImage(const android::emulation::control::Image& img);

    std::shared_ptr<EmulatorClient> client_;
    GrpcVideoSourceOptions options_;

    std::atomic<bool> capture_running_{false};
    std::thread capture_thread_;
    std::unique_ptr<::grpc::ClientContext> context_;

    std::unique_ptr<::goldfish::memory::SharedMemory> shared_memory_;
    std::unique_ptr<VideoFormatPipeline> pipeline_;
};

}  // namespace goldfish::videobridge
