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
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "goldfish/display/webm_encoder.h"

namespace goldfish::display {
// Callback signature:
// Params: (pixel_buffer, width, height, frame_index, timestamp_seconds)
using DrawCallback = std::function<void(uint8_t*, int, int, int, double)>;

class VideoRecorder {
  public:
    VideoRecorder(int width, int height, int fps, int bitrate, int duration);
    ~VideoRecorder();

    // Starts the recording process in a new background thread.
    // filename: Output path for the .webm file.
    // callback: Function that will be called 'fps' times a second to fill the buffer.
    bool Start(const std::string& filename, DrawCallback callback);

    // Signals the background thread to stop, waits for it to finish,
    // and flushes the encoder to disk.
    void Stop();

    // Returns true if the recording thread is currently active.
    bool IsRunning() const;

  private:  // The main loop that runs inside worker_thread_
    void GenerationLoop();

    // Configuration
    int width_;
    int height_;
    int fps_;
    int bitrate_;
    int duration_;  // seconds

    // State
    std::atomic<bool> running_{false};
    DrawCallback draw_callback_;

    // Resources
    std::thread worker_thread_;
    std::unique_ptr<WebMEncoder> encoder_;
};

}  // namespace goldfish::display
