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

namespace goldfish::imaging {

// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/common/aidl/android/hardware/graphics/common/PixelFormat.aidl
enum class AndroidPixelFormat {
    UNSPECIFIED = 0,
    RGBA_8888 = 0x1,
    YCBCR_420_888 = 0x23,  // arbitrary YUV layout
};

}  // namespace goldfish::imaging
