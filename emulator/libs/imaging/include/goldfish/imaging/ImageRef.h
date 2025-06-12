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

#include "goldfish/imaging/ImageFormat.h"
#include "goldfish/imaging/Rect.h"

namespace goldfish::imaging {

struct ImageRef {
  ImageRef() = default;

  ImageRef(ImageFormat format, Rect<uint32_t> size, const void* data, size_t dataSize)
      : mData(data), mDataSize(dataSize), mSize(size), mFormat(format) {}

  ImageFormat getFormat() const { return mFormat; }

  const Rect<uint32_t>& getSize() const { return mSize; }

  std::pair<const void*, size_t> getData() const { return {mData, mDataSize}; }

 private:
  const void* mData = nullptr;
  size_t mDataSize = 0;
  Rect<uint32_t> mSize;
  ImageFormat mFormat = ImageFormat::NONE;
};

}  // namespace goldfish::imaging
