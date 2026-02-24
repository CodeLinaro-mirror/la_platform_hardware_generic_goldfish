/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <utility>

#include "goldfish/imaging/rect.h"
#include "image_format.h"

namespace goldfish::imaging {

struct ImageRef {
    ImageRef() = default;

    ImageRef(ImageFormat format, Rect<uint32_t> size, const void* data, size_t data_size)
            : data_(data), data_size_(data_size), size_(size), format_(format) {}

    ImageFormat GetFormat() const { return format_; }

    const Rect<uint32_t>& GetSize() const { return size_; }

    std::pair<const void*, size_t> GetData() const { return {data_, data_size_}; }

  private:
    const void* data_ = nullptr;
    size_t data_size_ = 0;
    Rect<uint32_t> size_;
    ImageFormat format_ = ImageFormat::kNone;
};

}  // namespace goldfish::imaging
