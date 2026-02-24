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

size_t GetStride(const ImageFormat fmt, const size_t width) {
    switch (fmt) {
    case ImageFormat::kRgba8888:
        return width * 4;

    case ImageFormat::kYuV4203P:
    case ImageFormat::kYuV420NV12:
    case ImageFormat::kNone:
        break;
    }

    return 0;
}

size_t GetStride(const ImageRef& img) {
    return GetStride(img.GetFormat(), img.GetSize().width);
}

size_t GetDataSize(const ImageFormat fmt, const size_t width, const size_t height) {
    switch (fmt) {
    case ImageFormat::kNone:
        break;

    case ImageFormat::kRgba8888:
        return width * height * 4;

    case ImageFormat::kYuV4203P:
    case ImageFormat::kYuV420NV12:
        return width * height * 3 / 2;
    }

    return 0;
}

}  // namespace goldfish::imaging
