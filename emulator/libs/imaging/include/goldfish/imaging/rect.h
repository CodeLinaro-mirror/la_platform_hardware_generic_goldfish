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

namespace goldfish::imaging {

template <class T>
struct Rect {
    Rect() = default;
    Rect(T width_, T height_) : width(width_), height(height_) {}

    size_t area() const { return size_t(width) * size_t(height); }

    T width{};
    T height{};
};

}  // namespace goldfish::imaging
