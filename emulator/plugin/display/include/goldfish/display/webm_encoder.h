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
    WebMEncoder(std::string fname, int w, int h, int f, int br);
    ~WebMEncoder();

    // Initializes FFmpeg resources and starts the background thread.
    // Returns true if successful, false otherwise.
    bool Init();

    // Adds a frame to the encoding queue.
    void AddFrame(const uint8_t* rgb_data);

    // Signals the encoder to stop and waits for completion.
    void Finish();

  private:
    void EncodeLoop();
    void ProcessFrame(const std::vector<uint8_t>& raw_data, int64_t frame_index);
    void Cleanup();
    bool IsFrameAvailable() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(queue_mutex_);

    // Config
    std::string filename_;
    int width_, height_, fps_, bitrate_;
    bool is_initialized_ = false;

    // FFmpeg Pointers
    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* codec_ctx_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;

    // Threading
    std::thread encoder_thread_;
    absl::Mutex queue_mutex_;
    std::queue<std::vector<uint8_t>> frame_queue_ ABSL_GUARDED_BY(queue_mutex_);
    std::atomic<bool> stop_signal_ ABSL_GUARDED_BY(queue_mutex_){false};
    std::atomic<bool> finished_{false};
    int64_t pts_counter_ = 0;
};
}  // namespace goldfish::display
