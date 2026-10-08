// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iomanip>

#include "TestUtils.h"
#include "aemu/base/files/ScopedStdioFile.h"
#include "android/base/system.h"
#include "raw_image_sources/image_file/raw_image_file_source.h"

using android::base::ScopedStdioFile;
using android::base::System;

namespace android {
namespace ver {

static constexpr size_t kMaxVectorOutput = 128;

struct ImageTestParam {
    std::string filename;
    std::string goldenFilename;

    ImageTestParam(const char* filename, const char* goldenFilename)
            : filename(filename), goldenFilename(goldenFilename) {}
};

static void PrintTo(const ImageTestParam& param, std::ostream* os) {
    *os << "ImageTestParam(file=" << param.filename << ", golden=" << param.goldenFilename << ")";
}

static void PrintTo(const std::vector<uint8_t>& vec, std::ostream* os) {
    std::ios::fmtflags origFlags(os->flags());

    *os << '{';
    size_t count = 0;
    for (uint8_t val : vec) {
        if (count > 0) {
            *os << ", ";
        }

        if (count == kMaxVectorOutput) {
            *os << "... ";
            break;
        }

        if ((count % 16) == 0) {
            *os << "\n";
        }

        if (val == 0) {
            *os << "    ";
        } else {
            *os << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(val) << std::dec;
        }
    }

    *os << '}';

    os->flags(origFlags);
}

class RawImageFileSourceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mTempDir = std::filesystem::path(testing::TempDir()) / "raw_image_file_source_test";
        std::filesystem::create_directories(mTempDir);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(mTempDir, ec);
    }

    void createTempFile(const std::string& name, const std::vector<uint8_t>& contents,
                        std::string* path) {
        *path = (mTempDir / name).string();
        std::ofstream ofs(*path, std::ios::binary);
        ASSERT_TRUE(ofs.is_open()) << "Failed to create temp file: " << *path;
        ofs.write(reinterpret_cast<const char*>(contents.data()), contents.size());
        ofs.close();
    }

    std::filesystem::path mTempDir;
};

TEST_F(RawImageFileSourceTest, NonExistentFile) {
    std::string path = (mTempDir / "non_existent.png").string();
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, EmptyPNGFile) {
    std::string path;
    createTempFile("empty.png", {}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, TruncatedPNGFile) {
    // PNG signature but nothing else
    std::string path;
    createTempFile("truncated.png", {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, CorruptPNGFile) {
    // Correct signature but garbage data
    std::vector<uint8_t> data = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (int i = 0; i < 100; ++i) data.push_back(static_cast<uint8_t>(i));
    std::string path;
    createTempFile("corrupt.png", data, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, EmptyJPEGFile) {
    std::string path;
    createTempFile("empty.jpg", {}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, TruncatedJPEGFile) {
    // JPEG SOI marker (FF D8) but nothing else
    std::string path;
    createTempFile("truncated.jpg", {0xFF, 0xD8}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, CorruptJPEGFile) {
    // JPEG SOI marker followed by garbage
    std::vector<uint8_t> data = {0xFF, 0xD8};
    for (int i = 0; i < 100; ++i) data.push_back(static_cast<uint8_t>(i));
    std::string path;
    createTempFile("corrupt.jpg", data, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, UnsupportedExtension) {
    std::string path;
    createTempFile("test.txt", {'a', 'b', 'c'}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, NonImageWithValidExtension) {
    // It has a .jpg extension but is just a text file
    std::string path;
    createTempFile("not_a_jpeg.jpg", {'h', 'e', 'l', 'l', 'o'}, &path);
    auto source = RawImageFileSource::Create(path);
    EXPECT_EQ(source, nullptr);
}

TEST_F(RawImageFileSourceTest, ExifRotation) {
    // 1. Test JPEG
    {
        ImageResult base_golden;
        loadGoldenBmp("ver/exif_rotation/corner_jpeg_golden.bmp", &base_golden);

        for (int orientation = 1; orientation <= 8; ++orientation) {
            std::string path =
                    (std::filesystem::path(getTestProgramDirectory()) / "testdata" /
                     "ver/exif_rotation" / ("corner_" + std::to_string(orientation) + ".jpg"))
                            .string();

            auto source = RawImageFileSource::Create(path);
            ASSERT_NE(source, nullptr) << "Failed for JPEG orientation " << orientation;

            EXPECT_EQ(0, source->Start(VerImageFormat::RGBA8, 0, 0));

            std::vector<uint8_t> buffer;
            uint32_t image_width = 0;
            uint32_t image_height = 0;
            auto updater = [&](const RawImageBufferView* view) {
                buffer.assign(view->buffer, view->buffer + view->buffer_size);
                image_width = view->width;
                image_height = view->height;
                return absl::OkStatus();
            };

            auto tokenOrError = source->UpdateImage(0, std::nullopt, updater);
            ASSERT_TRUE(tokenOrError.ok());
            ASSERT_TRUE(tokenOrError.value().has_value());
            EXPECT_EQ(0, source->Stop());

            EXPECT_EQ(base_golden.width, image_width)
                    << "Failed for JPEG orientation " << orientation;
            EXPECT_EQ(base_golden.height, image_height)
                    << "Failed for JPEG orientation " << orientation;

            compareImageBuffers(buffer, base_golden.buffer, kImageCompareThreshold);
        }
    }

    // 2. Test PNG
    {
        ImageResult base_golden;
        loadGoldenBmp("ver/exif_rotation/corner_png_golden.bmp", &base_golden);

        for (int orientation = 1; orientation <= 8; ++orientation) {
            std::string path =
                    (std::filesystem::path(getTestProgramDirectory()) / "testdata" /
                     "ver/exif_rotation" / ("corner_" + std::to_string(orientation) + ".png"))
                            .string();

            auto source = RawImageFileSource::Create(path);
            ASSERT_NE(source, nullptr) << "Failed for PNG orientation " << orientation;

            EXPECT_EQ(0, source->Start(VerImageFormat::RGBA8, 0, 0));

            std::vector<uint8_t> buffer;
            uint32_t image_width = 0;
            uint32_t image_height = 0;
            auto updater = [&](const RawImageBufferView* view) {
                buffer.assign(view->buffer, view->buffer + view->buffer_size);
                image_width = view->width;
                image_height = view->height;
                return absl::OkStatus();
            };

            auto tokenOrError = source->UpdateImage(0, std::nullopt, updater);
            ASSERT_TRUE(tokenOrError.ok());
            ASSERT_TRUE(tokenOrError.value().has_value());
            EXPECT_EQ(0, source->Stop());

            EXPECT_EQ(base_golden.width, image_width)
                    << "Failed for PNG orientation " << orientation;
            EXPECT_EQ(base_golden.height, image_height)
                    << "Failed for PNG orientation " << orientation;

            compareImageBuffers(buffer, base_golden.buffer, kImageCompareThreshold);
        }
    }
}

class RawImageFileSourceCompareTest : public ::testing::TestWithParam<ImageTestParam> {
  protected:
    static std::string testdataPathToAbsolute(std::string_view filename) {
        return (std::filesystem::path(getTestProgramDirectory()) / "testdata" / filename).string();
    }
};

TEST_P(RawImageFileSourceCompareTest, Compare) {
    const ImageTestParam param = GetParam();
    std::string path = testdataPathToAbsolute(param.filename);
    auto source = RawImageFileSource::Create(path);
    ASSERT_NE(source, nullptr) << "Failed to load image: " << path;

    EXPECT_EQ(0, source->Start(VerImageFormat::RGBA8, 0, 0));

    std::vector<uint8_t> buffer;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    auto updater = [&](const RawImageBufferView* view) {
        buffer.assign(view->buffer, view->buffer + view->buffer_size);
        image_width = view->width;
        image_height = view->height;
        return absl::OkStatus();
    };

    auto tokenOrError = source->UpdateImage(0, std::nullopt, updater);
    ASSERT_TRUE(tokenOrError.ok());
    ASSERT_TRUE(tokenOrError.value().has_value());

    EXPECT_EQ(0, source->Stop());

    ImageResult golden;
    loadGoldenBmp(param.goldenFilename, &golden);

    ASSERT_EQ(golden.width, image_width);
    ASSERT_EQ(golden.height, image_height);

    compareImageBuffers(buffer, golden.buffer, kImageCompareThreshold, true);
}

INSTANTIATE_TEST_SUITE_P(
        RawImageFileSource, RawImageFileSourceCompareTest,
        ::testing::Values(
                ImageTestParam("gray_alpha.png", "gray_alpha_golden.bmp"),
                ImageTestParam("gray.png", "gray_golden.bmp"),
                ImageTestParam("indexed_alpha.png", "indexed_alpha_golden.bmp"),
                ImageTestParam("indexed.png", "indexed_golden.bmp"),
                ImageTestParam("interlaced.png", "interlaced_golden.bmp"),
                ImageTestParam("jpeg_gray.jpg", "jpeg_gray_golden.bmp"),
                ImageTestParam("jpeg_gray_progressive.jpg", "jpeg_gray_progressive_golden.bmp"),
                ImageTestParam("jpeg_rgb24.jpg", "jpeg_rgb24_golden.bmp"),
                ImageTestParam("jpeg_rgb24_progressive.jpg", "jpeg_rgb24_progressive_golden.bmp"),
                ImageTestParam("rgb24_31px.png", "rgb24_31px_golden.bmp"),
                ImageTestParam("rgba32.png", "rgba32_golden.bmp")));

}  // namespace ver
}  // namespace android
