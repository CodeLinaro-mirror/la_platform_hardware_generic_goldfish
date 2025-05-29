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

#include "android/camera/CameraDevice.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "goldfish/parsing/getKeyValueStr.h"
#include "goldfish/parsing/split2.h"

namespace goldfish::devices::camera {
using namespace std::literals;

using parsing::getKeyValueStr;
using parsing::split2;

CameraDevice::CameraDevice(SocketPtr socket, void* imageProvider,
                           const CameraImageProviderVtbl* vtbl, GrallocDetailsPtr grallocDetails)
        : CameraDeviceBase(std::move(socket), imageProvider, *vtbl, std::move(grallocDetails)) {}

bool CameraDevice::processQuery(const std::string_view query, const std::string_view params) {
    if (query == "capture"sv) {
        capture(params);
        return true;
    } else if (query == "configure"sv) {
        configure(params);
        return true;
    } else {
        return false;
    }
}

void CameraDevice::configure(const std::string_view params) {
    const std::optional<std::string_view> maybeStreams = getKeyValueStr(params, "streams"sv);
    if (!maybeStreams) {
        sendResponse(false, "missing 'streams' parameter"sv);
        return;
    }

    StreamCfgs streamCfgs;
    for (const std::string_view streamCfgStr : absl::StrSplit(maybeStreams.value(), ',')) {
        char strz[64];
        if (streamCfgStr.size() >= sizeof(strz)) {
            sendResponse(false, "a stream config is too long"sv);
            return;
        }

        ::memcpy(strz, streamCfgStr.data(), streamCfgStr.size());
        strz[streamCfgStr.size()] = 0;

        int32_t id;
        uint32_t width;
        uint32_t height;
        uint32_t aformat;
        if (4 != ::sscanf(strz, "%d:%ux%u@%X", &id, &width, &height, &aformat)) {
            sendResponse(false, "can't parse a stream config"sv);
            return;
        }

        const uint32_t fourCC = aFormatToFourCC(aformat);
        if (!fourCC) {
            sendResponse(false, "unsupported format"sv);
            return;
        }

        streamCfgs.push_back({
            .id = id,
            .format = fourCC,
            .size =
                    {
                        .width = static_cast<uint16_t>(width),
                        .height = static_cast<uint16_t>(height),
                    },
        });
    }

    const size_t nStreamCfgs = streamCfgs.size();
    if (!nStreamCfgs) {
        sendResponse(false, "no streams to configure"sv);
        return;
    }

    if (!mStreamCfgs.empty()) {
        stopCapturingImpl();
        mStreamCfgs.clear();
    }

    if (!startCapturingImpl(streamCfgs.data(), nStreamCfgs)) {
        sendResponse(false, "can't start capturing"sv);
        return;
    }

    mStreamCfgs = std::move(streamCfgs);
    sendResponse(true);
}

void CameraDevice::capture(const std::string_view params) {
    const std::optional<std::string_view> maybeBufs = getKeyValueStr(params, "bufs"sv);
    if (!maybeBufs) {
        sendResponse(false, "missing 'bufs' parameter"sv);
        return;
    }

    using BufInfo = std::pair<int, std::string_view>;
    using Bufs = std::vector<BufInfo>;

    Bufs bufs;
    for (const std::string_view biStr : absl::StrSplit(maybeBufs.value(), ',')) {
        const auto [idStr, handleStr] = split2(biStr, ':');

        int id;
        const auto [end, ec] = std::from_chars(&*idStr.begin(), &*idStr.end(), id);
        if ((ec != std::errc()) || (end != &*idStr.end())) {
            sendResponse(false, "can't parse a buf value"sv);
            return;
        }

        bufs.push_back({id, handleStr});
    }

    const size_t nBufs = bufs.size();
    if (!nBufs) {
        sendResponse(false, "no buffers to capture"sv);
        return;
    }

    std::vector<CameraImageProviderStreamCaptureInfo> scis(nBufs);
    for (size_t i = 0; i < nBufs; ++i) {
        BufInfo& bi = bufs[i];
        const int id = bi.first;
        const auto si = std::find_if(
                mStreamCfgs.begin(), mStreamCfgs.end(),
                [id](const CameraImageProviderStreamConfig& cfg) { return id == cfg.id; });
        if (si == mStreamCfgs.end()) {
            sendResponse(false, "can't find buf's stream"sv);
            return;
        }

        scis[i] = {
            .cfg = &*si,
            .bufOpaque = &bi.second,
        };
    }

    const CameraImageProviderCaptureOpts opts = {
        .whiteBalance =
                {
                    .red = 1.0f,
                    .green = 1.0f,
                    .blue = 1.0f,
                },
        .expComp = 1.0f,
    };

    if (!captureImpl(opts, scis.data(), nBufs)) {
        sendResponse(false, "capture failed"sv);
        return;
    }

    sendResponse(true);
}

}  // namespace goldfish::devices::camera
