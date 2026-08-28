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

#include "wav_writer.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#include "absl/status/status.h"

namespace android::emulation::tools {
namespace {

#pragma pack(push, 1)
struct WavHeader {
    char riff_tag[4] = {'R', 'I', 'F', 'F'};
    uint32_t riff_size = 0;  // 36 + data_size
    char wave_tag[4] = {'W', 'A', 'V', 'E'};
    char fmt_tag[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;  // 1 = PCM (uncompressed)
    uint16_t num_channels = 2;
    uint32_t sample_rate = 44100;
    uint32_t byte_rate = 44100 * 2 * 2;
    uint16_t block_align = 2 * 2;
    uint16_t bits_per_sample = 16;
    char data_tag[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};
#pragma pack(pop)

static_assert(sizeof(WavHeader) == 44, "WavHeader must be exactly 44 bytes");

WavHeader CreateWavHeader(uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample,
                          uint32_t data_size) {
    WavHeader header;
    header.riff_size = 36 + data_size;
    header.num_channels = num_channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = bits_per_sample;
    header.block_align = num_channels * (bits_per_sample / 8);
    header.byte_rate = sample_rate * header.block_align;
    header.data_size = data_size;
    return header;
}

}  // namespace

WavWriter::~WavWriter() {
    Close();
}

absl::Status WavWriter::Open(const std::filesystem::path& filepath, uint32_t sample_rate,
                             uint16_t num_channels, uint16_t bits_per_sample) {
    if (file_.is_open()) {
        Close();
    }

    filepath_ = filepath;
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
    bits_per_sample_ = bits_per_sample;
    bytes_written_ = 0;

    file_.open(filepath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return absl::InternalError("Failed to open file for writing: " + filepath.string());
    }

    // Write an initial header with the configured audio format and 0 data size.
    // The final payload size will be patched in Close(). Writing format metadata
    // upfront allows tools like Audacity, VLC, or ffplay to recover and parse
    // the audio stream even if the process is terminated abruptly before Close().
    WavHeader initial_header = CreateWavHeader(sample_rate_, num_channels_, bits_per_sample_, 0);
    file_.write(reinterpret_cast<const char*>(&initial_header), sizeof(initial_header));
    if (!file_.good()) {
        return absl::InternalError("Failed to write initial WAV header to: " + filepath.string());
    }

    return absl::OkStatus();
}

absl::Status WavWriter::Write(const void* data, size_t size_bytes) {
    if (!file_.is_open()) {
        return absl::FailedPreconditionError("WAV file is not open");
    }
    if (size_bytes == 0) {
        return absl::OkStatus();
    }

    file_.write(reinterpret_cast<const char*>(data), size_bytes);
    if (!file_.good()) {
        return absl::InternalError("Failed to write PCM data");
    }
    bytes_written_ += size_bytes;
    return absl::OkStatus();
}

void WavWriter::Close() {
    if (!file_.is_open()) {
        return;
    }

    WavHeader final_header = CreateWavHeader(sample_rate_, num_channels_, bits_per_sample_,
                                             static_cast<uint32_t>(bytes_written_));

    file_.seekp(0, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&final_header), sizeof(final_header));
    file_.flush();
    file_.close();
}

}  // namespace android::emulation::tools
