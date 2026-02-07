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

#pragma once

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace goldfish::display {

class WebMEncoder {
  public:
    WebMEncoder(const std::string& filename, int width, int height, int fps, int bitrate);
    ~WebMEncoder();

    // Initializes FFmpeg resources and starts the background thread.
    // Returns true if successful, false otherwise.
    bool init();

    // Adds a frame to the encoding queue.
    void addFrame(const uint8_t* rgbaData);

    // Signals the encoder to stop and waits for completion.
    void finish();

  private:
    void encodeLoop();
    void processFrame(const std::vector<uint8_t>& rawData, int64_t frameIndex);
    void cleanup();
    bool isFrameAvailable() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(queueMutex);

    // Config
    std::string filename;
    int width, height, fps, bitrate;
    bool is_initialized = false;

    // FFmpeg Pointers
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;

    // Threading
    std::thread encoderThread;
    absl::Mutex queueMutex;
    std::queue<std::vector<uint8_t>> frameQueue ABSL_GUARDED_BY(queueMutex);
    std::atomic<bool> stopSignal ABSL_GUARDED_BY(queueMutex) {false};
    std::atomic<bool> finished{false};
    int64_t ptsCounter = 0;
};
}  // namespace goldfish::display
