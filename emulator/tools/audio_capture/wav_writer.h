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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "absl/status/status.h"

namespace android::emulation::tools {

/**
 * @class WavWriter
 * @brief Streams and writes uncompressed raw PCM audio samples into a standard RIFF WAVE file.
 *
 * This class handles the lifecycle of writing standard 44-byte RIFF/WAVE container
 * headers and appending raw PCM audio payloads:
 *
 * 1. Upon @ref Open(), a valid 44-byte WAV header containing the configured sample rate,
 *    channel count, and bit depth is written to the beginning of the file (with data size
 * initialized to 0). Writing accurate format metadata upfront ensures media tools (Audacity, VLC,
 * ffplay) can recover and parse the audio file even if the process terminates abruptly before @ref
 * Close().
 * 2. Incoming raw PCM chunks are streamed directly to disk via @ref Write().
 * 3. Upon @ref Close() (or object destruction), the file stream seeks back to offset 0 and rewrites
 *    the header with the final payload and RIFF chunk sizes.
 */
class WavWriter {
  public:
    WavWriter() = default;

    /**
     * @brief Destructor. Closes the underlying file and finalizes WAV headers if still open.
     */
    ~WavWriter();

    /**
     * @brief Opens a new or existing file for binary WAV writing.
     *
     * Creates or truncates the specified file, writes an initial 44-byte WAV header
     * populated with the supplied audio format parameters, and prepares the writer for data.
     *
     * @param filepath The output filesystem path where the .wav file should be created.
     * @param sample_rate Sampling rate in Hz (e.g. 44100, 48000, 16000). Defaults to 44100.
     * @param num_channels Number of audio channels (1 for Mono, 2 for Stereo). Defaults to 2.
     * @param bits_per_sample Bit depth per PCM sample (e.g. 16 for S16). Defaults to 16.
     * @return absl::OkStatus() on success, or an error status if the file could not be created or
     * written.
     */
    absl::Status Open(const std::filesystem::path& filepath, uint32_t sample_rate = 44100,
                      uint16_t num_channels = 2, uint16_t bits_per_sample = 16);

    /**
     * @brief Appends raw PCM audio data to the open WAV file.
     *
     * @param data Pointer to the raw PCM byte buffer.
     * @param size_bytes Total number of bytes to write.
     * @return absl::OkStatus() on success, or an error status if the file is not open or the write
     * failed.
     */
    absl::Status Write(const void* data, size_t size_bytes);

    /**
     * @brief Finalizes the WAV header with total payload size and closes the file stream.
     *
     * Seeks to byte offset 0, patches the RIFF and data chunk size fields with the exact
     * total byte count written during the session, flushes all buffers, and closes the file.
     */
    void Close();

    /**
     * @brief Total number of raw PCM audio payload bytes written so far (excluding the 44-byte
     * header).
     */
    size_t bytes_written() const { return bytes_written_; }

    /**
     * @brief Sampling rate in Hz configured for this WAV file.
     */
    uint32_t sample_rate() const { return sample_rate_; }

    /**
     * @brief Number of audio channels configured for this WAV file.
     */
    uint16_t num_channels() const { return num_channels_; }

    /**
     * @brief Bit depth per audio sample configured for this WAV file.
     */
    uint16_t bits_per_sample() const { return bits_per_sample_; }

  private:
    std::ofstream file_;
    std::filesystem::path filepath_;
    uint32_t sample_rate_{44100};
    uint16_t num_channels_{2};
    uint16_t bits_per_sample_{16};
    size_t bytes_written_{0};
};

}  // namespace android::emulation::tools
