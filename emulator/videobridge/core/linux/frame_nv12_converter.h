// Copyright (C) 2026 The Android Open Source Project
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
#pragma once

#include <cstdint>
#include <cstring>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "libyuv/convert.h"
#include "libyuv/planar_functions.h"

namespace goldfish::videobridge {

/**
 * @brief Ingests a WebRTC VideoFrame into a destination NV12 memory buffer.
 *
 * Direct-copies NV12 frame buffers with a single-burst std::memcpy fast-path for
 * contiguous memory layouts, falling back to libyuv::CopyPlane or I420ToNV12 conversion.
 */
inline bool CopyOrConvertFrameToNv12(const webrtc::VideoFrame& frame, uint8_t* dst_y,
                                     int dst_stride_y, uint8_t* dst_uv, int dst_stride_uv,
                                     int width, int height) {
    auto frame_buffer = frame.video_frame_buffer();
    if (!frame_buffer) return false;

    if (frame_buffer->type() == webrtc::VideoFrameBuffer::Type::kNV12) {
        const auto* nv12 = static_cast<const webrtc::NV12BufferInterface*>(frame_buffer.get());

        const int uv_width_bytes = ((width + 1) / 2) * 2;
        const int uv_height_rows = (height + 1) / 2;
        const size_t y_plane_bytes = static_cast<size_t>(width) * height;
        const size_t uv_plane_bytes = static_cast<size_t>(uv_width_bytes) * uv_height_rows;

        // Contiguous memory fast-path: When source and destination strides match the image
        // width without padding, copy the entire Y+UV frame in a single contiguous memory burst.
        if (nv12->StrideY() == width && dst_stride_y == width && nv12->StrideUV() == width &&
            dst_stride_uv == width && nv12->DataUV() == nv12->DataY() + y_plane_bytes &&
            dst_uv == dst_y + y_plane_bytes) {
            std::memcpy(dst_y, nv12->DataY(), y_plane_bytes + uv_plane_bytes);
            return true;
        }

        libyuv::CopyPlane(nv12->DataY(), nv12->StrideY(), dst_y, dst_stride_y, width, height);
        libyuv::CopyPlane(nv12->DataUV(), nv12->StrideUV(), dst_uv, dst_stride_uv, uv_width_bytes,
                          uv_height_rows);
        return true;
    }

    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = frame_buffer->ToI420();
    if (!i420) return false;

    return libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(),
                              i420->DataV(), i420->StrideV(), dst_y, dst_stride_y, dst_uv,
                              dst_stride_uv, width, height) == 0;
}

}  // namespace goldfish::videobridge
