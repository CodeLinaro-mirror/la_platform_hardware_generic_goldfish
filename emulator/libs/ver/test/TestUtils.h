/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <gtest/gtest.h>
#include <png.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "aemu/base/files/ScopedStdioFile.h"
#include "android/base/bazel_info.h"
#include "android/base/system.h"
#include "ver/virtual_environment_renderer.h"

namespace android {
namespace ver {

inline std::string getTestProgramDirectory() {
    return ::android::base::Bazel::RunfilesPath("goldfish+/emulator/libs/ver");
}

struct ImageResult {
    std::vector<uint8_t> buffer;
    uint32_t width = 0;
    uint32_t height = 0;
};

inline uint32_t readUint32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

inline uint16_t readUint16LE(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

inline int32_t readInt32LE(const uint8_t* data) {
    return static_cast<int32_t>(readUint32LE(data));
}

inline bool saveRgbaPng(const char* filename, int width, int height, const uint8_t* rgbaData) {
    ::android::base::ScopedStdioFile fp(fopen(filename, "wb"));
    if (!fp) {
        return false;
    }
    png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!pngPtr) {
        return false;
    }
    png_infop infoPtr = png_create_info_struct(pngPtr);
    if (!infoPtr) {
        png_destroy_write_struct(&pngPtr, nullptr);
        return false;
    }
    if (setjmp(png_jmpbuf(pngPtr))) {
        png_destroy_write_struct(&pngPtr, &infoPtr);
        return false;
    }
    png_init_io(pngPtr, fp.get());
    png_set_IHDR(pngPtr, infoPtr, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(pngPtr, infoPtr);
    for (int y = 0; y < height; ++y) {
        png_write_row(pngPtr, const_cast<png_bytep>(rgbaData + y * width * 4));
    }
    png_write_end(pngPtr, nullptr);
    png_destroy_write_struct(&pngPtr, &infoPtr);
    return true;
}

inline bool loadGoldenBmp(std::string_view filename, ImageResult* result) {
    using namespace ::android::base;
    const std::string path =
            (std::filesystem::path(getTestProgramDirectory()) / "testdata" / filename).string();

    constexpr size_t kBmpHeaderSize = 54;

    ScopedStdioFile fp(fopen(path.c_str(), "rb"));
    if (!fp) {
        fprintf(stderr, "Failed to open golden image: %s\n", path.c_str());
        return false;
    }

    uint8_t header[kBmpHeaderSize];
    if (fread(header, 1, sizeof(header), fp.get()) != kBmpHeaderSize) {
        fprintf(stderr, "Failed to read BMP header from: %s\n", path.c_str());
        return false;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        fprintf(stderr, "Invalid BMP header for: %s\n", path.c_str());
        return false;
    }

    uint32_t dataPos = readUint32LE(&header[0x0A]);
    uint32_t imageSize = readUint32LE(&header[0x22]);
    const uint16_t bitsPerPixel = readUint16LE(&header[0x1C]);
    int32_t width = readInt32LE(&header[0x12]);
    int32_t height = readInt32LE(&header[0x16]);

    if (height < 0) {
        height = -height;
    }

    const size_t bytesPerPixel = bitsPerPixel / 8;
    const size_t stride = (width * bytesPerPixel + 3) / 4 * 4;
    if (imageSize < stride * height) {
        imageSize = stride * height;
    }

    if (dataPos < kBmpHeaderSize) {
        dataPos = kBmpHeaderSize;
    }

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        fprintf(stderr, "Invalid bits per pixel: %d for %s\n", bitsPerPixel, path.c_str());
        return false;
    }

    result->width = width;
    result->height = height;

    std::vector<uint8_t> rawData(imageSize);
    if (fseek(fp.get(), dataPos, SEEK_SET) != 0 ||
        fread(rawData.data(), 1, imageSize, fp.get()) != imageSize) {
        fprintf(stderr, "Failed to read BMP data from: %s\n", path.c_str());
        return false;
    }
    fp.reset();

    result->buffer.resize(width * height * 4);

    // BMP is bottom-up, but output is top-down.
    // Swizzle from BGR(A) to RGBA and flip vertically.
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = rawData.data() + (height - 1 - y) * stride;
        uint8_t* dstRow = result->buffer.data() + y * width * 4;

        for (int x = 0; x < width; ++x) {
            const uint8_t* srcPixel = srcRow + x * bytesPerPixel;
            uint8_t* dstPixel = dstRow + x * 4;

            if (bitsPerPixel == 32) {
                // The 32-bit BMPs in this project appear to be stored as ABGR.
                // Swizzle ABGR to RGBA.
                dstPixel[0] = srcPixel[3];  // R
                dstPixel[1] = srcPixel[2];  // G
                dstPixel[2] = srcPixel[1];  // B
                dstPixel[3] = srcPixel[0];  // A
            } else {
                // BGR to RGBA
                dstPixel[0] = srcPixel[2];  // R
                dstPixel[1] = srcPixel[1];  // G
                dstPixel[2] = srcPixel[0];  // B
                dstPixel[3] = 0xFF;         // A
            }
        }
    }
    return true;
}

inline bool loadGoldenPng(std::string_view filename, ImageResult* result) {
    using namespace ::android::base;
    const std::string path =
            (std::filesystem::path(getTestProgramDirectory()) / "testdata" / filename).string();
    int goldenWidth = 0, goldenHeight = 0, goldenBpp = 0;
    std::vector<uint8_t> goldenBuffer;
    if (!ver_texture_utils_load_png(path.c_str(), &goldenWidth, &goldenHeight, &goldenBpp,
                                    &goldenBuffer)) {
        return false;
    }
    result->width = goldenWidth;
    result->height = goldenHeight;
    size_t actualSize = goldenWidth * goldenHeight * 4;
    result->buffer.resize(actualSize);
    if (goldenBpp == 3) {
        for (int i = 0; i < goldenWidth * goldenHeight; ++i) {
            result->buffer[i * 4 + 0] = goldenBuffer[i * 3 + 0];
            result->buffer[i * 4 + 1] = goldenBuffer[i * 3 + 1];
            result->buffer[i * 4 + 2] = goldenBuffer[i * 3 + 2];
            result->buffer[i * 4 + 3] = 255;
        }
    } else {
        result->buffer = std::move(goldenBuffer);
    }
    return true;
}

inline bool loadGoldenImage(std::string_view filename, ImageResult* result) {
    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".bmp") {
        return loadGoldenBmp(filename, result);
    }
    return loadGoldenPng(filename, result);
}

// Threshold for 2D image golden comparison (Mean of Squared Differences per byte)
static constexpr double kImageCompareThreshold = 0.125;

// Threshold for 3D scene golden image comparison (Sum of Squared Differences per pixel)
static constexpr double kSceneCompareThreshold = 512.0;

inline double calculateImageSSD(const uint8_t* actual, const uint8_t* golden, size_t byteCount,
                                bool premultiplyAlpha = false) {
    double sum = 0.0;
    for (size_t i = 0; i < byteCount; i += 4) {
        for (size_t j = 0; j < 3; ++j) {
            double diff;
            if (premultiplyAlpha) {
                // Premultiply by alpha to ignore differences in transparent pixels.
                double val = static_cast<double>(actual[i + j]) * actual[i + 3] / 255.0;
                double goldVal = static_cast<double>(golden[i + j]) * golden[i + 3] / 255.0;
                diff = val - goldVal;
            } else {
                diff = static_cast<double>(actual[i + j]) - golden[i + j];
            }
            sum += diff * diff;
        }
        // Also compare alpha itself.
        double alphaDiff = static_cast<double>(actual[i + 3]) - golden[i + 3];
        sum += alphaDiff * alphaDiff;
    }
    return sum;
}

inline double calculateImageMSE(const uint8_t* actual, const uint8_t* golden, size_t byteCount,
                                bool premultiplyAlpha = false) {
    return (byteCount == 0)
                   ? 0.0
                   : calculateImageSSD(actual, golden, byteCount, premultiplyAlpha) / byteCount;
}

inline void compareImageBuffers(const uint8_t* actualData, size_t actualSize,
                                const uint8_t* goldenData, size_t goldenSize, double threshold,
                                bool premultiplyAlpha = false) {
    ASSERT_EQ(goldenSize, actualSize);
    double mse = calculateImageMSE(actualData, goldenData, actualSize, premultiplyAlpha);
    if (mse > threshold) {
        EXPECT_LE(mse, threshold);
        std::vector<uint8_t> actualVec(actualData, actualData + actualSize);
        std::vector<uint8_t> goldenVec(goldenData, goldenData + goldenSize);
        // Fall back to standard ASSERT_EQ for detailed diff output.
        ASSERT_EQ(goldenVec, actualVec);
    }
}

inline void compareImageBuffers(const std::vector<uint8_t>& actual,
                                const std::vector<uint8_t>& golden, double threshold,
                                bool premultiplyAlpha = false) {
    compareImageBuffers(actual.data(), actual.size(), golden.data(), golden.size(), threshold,
                        premultiplyAlpha);
}

inline void setupTestLibraryPaths() {
    using namespace ::android::base;
    std::filesystem::path lib64dir = std::filesystem::path(getTestProgramDirectory()) / "lib64";
    System::Get()->AddLibrarySearchDir(lib64dir.string());

    std::filesystem::path vulkanDir = lib64dir / "vulkan";
    System::Get()->AddLibrarySearchDir(vulkanDir.string());
}

inline bool compareWithGolden(const uint8_t* actualData, int width, int height,
                              const std::string& goldenFilename,
                              double threshold = kSceneCompareThreshold,
                              bool premultiplyAlpha = false) {
    ImageResult golden;
    if (!loadGoldenImage(goldenFilename, &golden)) {
        fprintf(stderr, "Failed to load golden image: %s\n", goldenFilename.c_str());
        return false;
    }

    if (width != static_cast<int>(golden.width) || height != static_cast<int>(golden.height)) {
        fprintf(stderr, "Dimensions mismatch: actual(%dx%d) vs golden(%dx%d)\n", width, height,
                golden.width, golden.height);
        return false;
    }

    size_t actualSize = width * height * 4;
    return calculateImageMSE(actualData, golden.buffer.data(), actualSize, premultiplyAlpha) <=
           threshold;
}

inline void verifyOrCompareWithGolden(const uint8_t* fbData, int width, int height,
                                      const std::string& goldenFilename,
                                      const std::string& testLabel,
                                      double threshold = kSceneCompareThreshold,
                                      bool premultiplyAlpha = false) {
    using namespace ::android::base;
    std::string goldenPath =
            (std::filesystem::path(getTestProgramDirectory()) / "testdata" / goldenFilename)
                    .string();

    if (!std::filesystem::exists(goldenPath)) {
        std::string outputPath =
                (System::Get()->GetTempDir() / "ver_test_outputs" / (testLabel + "_output.png"))
                        .string();
        std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
        saveRgbaPng(outputPath.c_str(), width, height, fbData);
        FAIL() << "Golden image not found at: " << goldenPath
               << ". Output saved to: " << outputPath;
    }

    bool match =
            compareWithGolden(fbData, width, height, goldenFilename, threshold, premultiplyAlpha);

    if (!match) {
        std::string outputPath =
                (System::Get()->GetTempDir() / "ver_test_outputs" / (testLabel + "_failed.png"))
                        .string();
        std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
        saveRgbaPng(outputPath.c_str(), width, height, fbData);
        FAIL() << "Golden image comparison failed for: " << testLabel
               << ". Output saved to: " << outputPath;
    }
}

}  // namespace ver
}  // namespace android
