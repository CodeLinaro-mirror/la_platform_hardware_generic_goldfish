// Copyright 2025 The Android Open Source Project
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

#include "goldfish/imaging/query_info.h"

namespace goldfish::imaging {

size_t getStride(const ImageFormat fmt, const size_t width) {
    switch (fmt) {
    case ImageFormat::RGBA_8888:
        return width * 4;

    case ImageFormat::YUV420_3P:
    case ImageFormat::YUV420_NV12:
    case ImageFormat::NONE:
        break;
    }

    return 0;
}

size_t getStride(const ImageRef& img) {
    return getStride(img.getFormat(), img.getSize().width);
}

size_t getDataSize(const ImageFormat fmt, const size_t width, const size_t height) {
    switch (fmt) {
    case ImageFormat::NONE:
        break;

    case ImageFormat::RGBA_8888:
        return width * height * 4;

    case ImageFormat::YUV420_3P:
    case ImageFormat::YUV420_NV12:
        return width * height * 3 / 2;
    }

    return 0;
}

}  // namespace goldfish::imaging
