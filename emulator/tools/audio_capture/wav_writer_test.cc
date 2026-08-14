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

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace android::emulation::tools {
namespace {

#pragma pack(push, 1)
struct TestWavHeader {
    char riff_tag[4];
    uint32_t riff_size;
    char wave_tag[4];
    char fmt_tag[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_tag[4];
    uint32_t data_size;
};
#pragma pack(pop)

static_assert(sizeof(TestWavHeader) == 44, "TestWavHeader must be 44 bytes");

class WavWriterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_file_ = std::filesystem::temp_directory_path() / "test_capture.wav";
        std::filesystem::remove(test_file_);
    }

    void TearDown() override { std::filesystem::remove(test_file_); }

    std::filesystem::path test_file_;
};

TEST_F(WavWriterTest, WritesValidWavHeaderAndData) {
    WavWriter writer;
    ASSERT_TRUE(writer.Open(test_file_.string(), 48000, 2, 16).ok());

    std::vector<int16_t> samples = {100, -100, 200, -200, 300, -300};
    ASSERT_TRUE(writer.Write(samples.data(), samples.size() * sizeof(int16_t)).ok());
    writer.Close();

    EXPECT_EQ(writer.bytes_written(), samples.size() * sizeof(int16_t));

    // Validate file contents
    std::ifstream in(test_file_, std::ios::binary);
    ASSERT_TRUE(in.is_open());

    TestWavHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    ASSERT_EQ(in.gcount(), sizeof(header));

    EXPECT_EQ(std::string(header.riff_tag, 4), "RIFF");
    EXPECT_EQ(header.riff_size, 36 + samples.size() * sizeof(int16_t));
    EXPECT_EQ(std::string(header.wave_tag, 4), "WAVE");
    EXPECT_EQ(std::string(header.fmt_tag, 4), "fmt ");
    EXPECT_EQ(header.fmt_size, 16);
    EXPECT_EQ(header.audio_format, 1);
    EXPECT_EQ(header.num_channels, 2);
    EXPECT_EQ(header.sample_rate, 48000);
    EXPECT_EQ(header.bits_per_sample, 16);
    EXPECT_EQ(header.byte_rate, 48000 * 2 * 2);
    EXPECT_EQ(header.block_align, 4);
    EXPECT_EQ(std::string(header.data_tag, 4), "data");
    EXPECT_EQ(header.data_size, samples.size() * sizeof(int16_t));

    std::vector<int16_t> read_samples(samples.size());
    in.read(reinterpret_cast<char*>(read_samples.data()), read_samples.size() * sizeof(int16_t));
    EXPECT_EQ(read_samples, samples);
}

}  // namespace
}  // namespace android::emulation::tools
