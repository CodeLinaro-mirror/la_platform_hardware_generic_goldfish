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

typedef enum GOLDFISH_IMAGE_FORMAT {
    GOLDFISH_IMAGE_FORMAT_NONE,
    GOLDFISH_IMAGE_FORMAT_RGBA_8888,
    GOLDFISH_IMAGE_FORMAT_YUV420_3P,    // 3 planes: Y, U and V
    GOLDFISH_IMAGE_FORMAT_YUV420_NV12,  // 2 planes: Y and UV (interleaved)
} GOLDFISH_IMAGE_FORMAT;

#ifdef __cplusplus

namespace goldfish::imaging {

enum class ImageFormat {
    NONE = GOLDFISH_IMAGE_FORMAT_NONE,
    RGBA_8888 = GOLDFISH_IMAGE_FORMAT_RGBA_8888,
    YUV420_3P = GOLDFISH_IMAGE_FORMAT_YUV420_3P,
    YUV420_NV12 = GOLDFISH_IMAGE_FORMAT_YUV420_NV12,
};

}  // namespace goldfish::imaging

#endif  //  __cplusplus
