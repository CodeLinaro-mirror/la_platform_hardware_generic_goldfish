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

#include "goldfish/imaging/to_vk_format.h"

namespace goldfish::imaging {

VkFormat toVkFormat(const ImageFormat fmt) {
    switch (fmt) {
    case ImageFormat::NONE:
        break;
    case ImageFormat::RGBA_8888:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::YUV420_3P:
        return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    case ImageFormat::YUV420_NV12:
        return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    }

    return VK_FORMAT_UNDEFINED;
}

}  // namespace goldfish::imaging