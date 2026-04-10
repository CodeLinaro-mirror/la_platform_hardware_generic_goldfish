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

#include "goldfish/display/video_recorder.h"

#include <chrono>
#include <utility>

#include "absl/log/log.h"

namespace goldfish::display {

VideoRecorder::VideoRecorder(int width, int height, int fps, int bitrate, int duration)
        : width_(width), height_(height), fps_(fps), bitrate_(bitrate), duration_(duration) {}

VideoRecorder::~VideoRecorder() {
    Stop();
}

bool VideoRecorder::Start(const std::string& filename, DrawCallback callback) {
    if (running_) {
        LOG(WARNING) << "[Recorder] Already running.\n";
        return false;
    }

    // 1. Create and Init Encoder
    encoder_ = std::make_unique<WebMEncoder>(filename, width_, height_, fps_, bitrate_);
    if (!encoder_->Init()) {
        LOG(WARNING) << "[Recorder] Failed to initialize encoder.\n";
        return false;
    }

    // 2. Setup State
    draw_callback_ = std::move(callback);
    running_ = true;

    // 3. Launch Generation Thread
    worker_thread_ = std::thread(&VideoRecorder::GenerationLoop, this);

    VLOG(2) << "[Recorder] initialized encoder.\n";
    return true;
}

void VideoRecorder::Stop() {
    if (!running_) return;

    VLOG(2) << "stop called \n";
    // 1. Signal thread to stop
    running_ = false;

    VLOG(2) << " wait for worker_thread_ \n";
    // 2. Wait for the generation thread to finish its current loop
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    VLOG(2) << " done \n";
}

bool VideoRecorder::IsRunning() const {
    return running_;
}

void VideoRecorder::GenerationLoop() {
    if (fps_ <= 0) {
        LOG(ERROR) << "[Recorder] Invalid FPS: " << fps_;
        running_ = false;
        return;
    }

    // Pre-allocate buffer to avoid allocation inside the loop
    std::vector<uint8_t> frame_buffer(static_cast<size_t>(width_) * height_ * 3);

    int frame_index = 0;
    auto frame_duration = std::chrono::milliseconds(1000 / fps_);

    const int total_frames = fps_ * duration_;
    while (running_) {
        auto start_time = std::chrono::high_resolution_clock::now();

        // 1. Calculate time
        const double time_sec = frame_index / static_cast<double>(fps_);

        // 2. Execute User Callback (Draw the frame)
        if (draw_callback_) {
            draw_callback_(frame_buffer.data(), width_, height_, frame_index, time_sec);
        }

        // 3. Send to Encoder
        if (encoder_) {
            encoder_->AddFrame(frame_buffer.data());
        }

        frame_index++;
        if (total_frames > 0 && frame_index > total_frames) {
            break;
        }

        // 4. Frame Pacing
        auto end_time = std::chrono::high_resolution_clock::now();
        auto work_duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        if (work_duration < frame_duration) {
            std::this_thread::sleep_for(frame_duration - work_duration);
        }
    }

    VLOG(2) << " wait for encoder \n";
    // 3. Tell the encoder to Finish writing the file
    if (encoder_) {
        encoder_->Finish();
        encoder_.reset();  // Release memory
    }
}

}  // namespace goldfish::display
