// Copyright 2026 The Android Open Source Project
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

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "raw_image_source.h"
#include "ver/virtual_environment_renderer_types.h"

#ifdef __linux__
#include <linux/videodev2.h>
#endif

#ifndef v4l2_fourcc
#define v4l2_fourcc(a, b, c, d)                                          \
    ((static_cast<uint32_t>(a) << 0) | (static_cast<uint32_t>(b) << 8) | \
     (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24))
#endif

#ifndef V4L2_PIX_FMT_RGB32
#define V4L2_PIX_FMT_RGB32 v4l2_fourcc('R', 'G', 'B', '4')
#endif
#ifndef V4L2_PIX_FMT_BGR32
#define V4L2_PIX_FMT_BGR32 v4l2_fourcc('B', 'G', 'R', '4')
#endif
#ifndef V4L2_PIX_FMT_ARGB32
#define V4L2_PIX_FMT_ARGB32 v4l2_fourcc('B', 'A', '2', '4')
#endif
#ifndef V4L2_PIX_FMT_RGB24
#define V4L2_PIX_FMT_RGB24 v4l2_fourcc('R', 'G', 'B', '3')
#endif
#ifndef V4L2_PIX_FMT_BGR24
#define V4L2_PIX_FMT_BGR24 v4l2_fourcc('B', 'G', 'R', '3')
#endif
#ifndef V4L2_PIX_FMT_YUYV
#define V4L2_PIX_FMT_YUYV v4l2_fourcc('Y', 'U', 'Y', 'V')
#endif
#ifndef V4L2_PIX_FMT_YUY2
#define V4L2_PIX_FMT_YUY2 v4l2_fourcc('Y', 'U', 'Y', '2')
#endif
#ifndef V4L2_PIX_FMT_YUNV
#define V4L2_PIX_FMT_YUNV v4l2_fourcc('Y', 'U', 'N', 'V')
#endif
#ifndef V4L2_PIX_FMT_V422
#define V4L2_PIX_FMT_V422 v4l2_fourcc('V', '4', '2', '2')
#endif
#ifndef V4L2_PIX_FMT_UYVY
#define V4L2_PIX_FMT_UYVY v4l2_fourcc('U', 'Y', 'V', 'Y')
#endif
#ifndef V4L2_PIX_FMT_YUV420
#define V4L2_PIX_FMT_YUV420 v4l2_fourcc('Y', 'U', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_YVU420
#define V4L2_PIX_FMT_YVU420 v4l2_fourcc('Y', 'V', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_NV12
#define V4L2_PIX_FMT_NV12 v4l2_fourcc('N', 'V', '1', '2')
#endif
#ifndef V4L2_PIX_FMT_NV21
#define V4L2_PIX_FMT_NV21 v4l2_fourcc('N', 'V', '2', '1')
#endif
#ifndef V4L2_PIX_FMT_MJPEG
#define V4L2_PIX_FMT_MJPEG v4l2_fourcc('M', 'J', 'P', 'G')
#endif
#ifndef V4L2_PIX_FMT_JPEG
#define V4L2_PIX_FMT_JPEG v4l2_fourcc('J', 'P', 'E', 'G')
#endif

namespace android {
namespace ver {

struct RawImageBufferViewFourCC {
    const uint8_t* buffer;
    size_t buffer_size;
    uint32_t pixel_format;
    int width;
    int height;
};

std::string FourccToString(uint32_t fourcc);
VerImageFormat FourccToVerImageFormat(uint32_t fourcc);
uint32_t VerImageFormatToFourcc(VerImageFormat ver_image_format);
RawImageBufferView RawImageBufferViewFourCCBridge(RawImageBufferViewFourCC* fourcc);

// Converts the provided buffer to V4L2_PIX_FMT_RGB32 (RGBA8 in memory).
// If conversion succeeds, `buffer` is updated to reference `conversion_storage`,
// resizing `conversion_storage` when needed without intermediate YUV staging.
int ConvertBufferToRGB32(RawImageBufferViewFourCC& buffer,
                         std::vector<uint8_t>& conversion_storage);

}  // namespace ver
}  // namespace android
