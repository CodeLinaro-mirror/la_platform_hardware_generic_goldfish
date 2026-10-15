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

#include "goldfish/videobridge/webrtc_logging.h"

#include <string_view>

#include "absl/log/log.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "rtc_base/logging.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {
namespace {

class AbseilLogSink : public ::webrtc::LogSink {
  public:
    void OnLogMessage(const std::string& message) override {
        std::string_view msg = message;
        if (!msg.empty() && msg.back() == '\n') {
            msg.remove_suffix(1);
        }
        VLOG(2) << "[WebRTC] " << msg;
    }

    void OnLogMessage(const std::string& message, ::webrtc::LoggingSeverity severity) override {
        std::string_view msg = message;
        if (!msg.empty() && msg.back() == '\n') {
            msg.remove_suffix(1);
        }
        switch (severity) {
        case ::webrtc::LS_VERBOSE:
            VLOG(2) << "[WebRTC] " << msg;
            break;
        case ::webrtc::LS_INFO:
            VLOG(1) << "[WebRTC] " << msg;
            break;
        case ::webrtc::LS_WARNING:
            LOG(WARNING) << "[WebRTC] " << msg;
            break;
        case ::webrtc::LS_ERROR:
            LOG(ERROR) << "[WebRTC] " << msg;
            break;
        default:
            break;
        }
    }
};

AbseilLogSink g_abseil_log_sink;

::webrtc::LoggingSeverity ParseWebRtcLoggingSeverity(std::string_view level) {
    if (level.empty() || level == "verbose") {
        return ::webrtc::LS_VERBOSE;
    }
    if (level == "info") {
        return ::webrtc::LS_INFO;
    }
    if (level == "warning") {
        return ::webrtc::LS_WARNING;
    }
    if (level == "error") {
        return ::webrtc::LS_ERROR;
    }
    if (level == "none") {
        return ::webrtc::LS_NONE;
    }
    return ::webrtc::LS_WARNING;
}

}  // namespace

void ConfigureWebRtcLogging(std::string_view level) {
    static bool initialized = false;
    if (initialized && level.empty()) {
        return;
    }
    if (initialized) {
        ::webrtc::LogMessage::RemoveLogToStream(&g_abseil_log_sink);
    }
    initialized = true;
    ::webrtc::LogMessage::LogToDebug(::webrtc::LS_NONE);
    ::webrtc::LogMessage::SetLogToStderr(false);
    ::webrtc::LogMessage::AddLogToStream(&g_abseil_log_sink, ParseWebRtcLoggingSeverity(level));
}

}  // namespace goldfish::videobridge
