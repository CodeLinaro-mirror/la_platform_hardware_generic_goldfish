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

#include "absl/log/log.h"

namespace goldfish::display {

VideoRecorder::VideoRecorder(int w, int h, int f, int br, int sec)
        : width(w), height(h), fps(f), bitrate(br), duration(sec) {}

VideoRecorder::~VideoRecorder() {
    stop();
}

bool VideoRecorder::start(const std::string& filename, DrawCallback callback) {
    if (running) {
        LOG(WARNING) << "[Recorder] Already running." << std::endl;
        return false;
    }

    // 1. Create and Init Encoder
    encoder = std::make_unique<WebMEncoder>(filename, width, height, fps, bitrate);
    if (!encoder->init()) {
        LOG(WARNING) << "[Recorder] Failed to initialize encoder." << std::endl;
        return false;
    }

    // 2. Setup State
    drawCallback = callback;
    running = true;

    // 3. Launch Generation Thread
    workerThread = std::thread(&VideoRecorder::generationLoop, this);

    VLOG(2) << "[Recorder] initialized encoder." << std::endl;
    return true;
}

void VideoRecorder::stop() {
    if (!running) return;

    VLOG(2) << "stop called " << std::endl;
    // 1. Signal thread to stop
    running = false;

    VLOG(2) << " wait for workerThread " << std::endl;
    // 2. Wait for the generation thread to finish its current loop
    if (workerThread.joinable()) {
        workerThread.join();
    }

    VLOG(2) << " done " << std::endl;
}

bool VideoRecorder::isRunning() const {
    return running;
}

void VideoRecorder::generationLoop() {
    // Pre-allocate buffer to avoid allocation inside the loop
    std::vector<uint8_t> frameBuffer(width * height * 3);

    int frameIndex = 0;
    auto frameDuration = std::chrono::milliseconds(1000 / fps);

    int total_frames = fps * duration;
    while (running) {
        auto startTime = std::chrono::high_resolution_clock::now();

        // 1. Calculate time
        double timeSec = frameIndex / static_cast<double>(fps);

        // 2. Execute User Callback (Draw the frame)
        if (drawCallback) {
            drawCallback(frameBuffer.data(), width, height, frameIndex, timeSec);
        }

        // 3. Send to Encoder
        if (encoder) {
            encoder->addFrame(frameBuffer.data());
        }

        frameIndex++;
        if (total_frames > 0 && frameIndex > total_frames) {
            break;
        }

        // 4. Frame Pacing
        auto endTime = std::chrono::high_resolution_clock::now();
        auto workDuration =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        if (workDuration < frameDuration) {
            std::this_thread::sleep_for(frameDuration - workDuration);
        }
    }

    VLOG(2) << " wait for encoder " << std::endl;
    // 3. Tell the encoder to finish writing the file
    if (encoder) {
        encoder->finish();
        encoder.reset();  // Release memory
    }
}

}  // namespace goldfish::display
