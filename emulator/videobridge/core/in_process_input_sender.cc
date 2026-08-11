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

#include "goldfish/videobridge/in_process_input_sender.h"

#include <utility>

#include "absl/log/log.h"

namespace goldfish::videobridge {

InProcessInputSender::InProcessInputSender(EventDispatcher dispatcher)
        : dispatcher_(std::move(dispatcher)) {}

absl::Status InProcessInputSender::Start() {
    started_ = true;
    VLOG(1) << "InProcessInputSender started.";
    return absl::OkStatus();
}

void InProcessInputSender::SendEvent(const InputEvent& event) {
    if (!started_) {
        LOG(WARNING) << "Dropping WebRTC input event (" << event.ShortDebugString()
                     << "): input delivery is not active.";
        return;
    }
    if (dispatcher_) {
        if (auto status = dispatcher_(event); !status.ok()) {
            LOG(WARNING) << "Failed to dispatch WebRTC input event (" << event.ShortDebugString()
                         << "): " << status;
        }
    }
}

void InProcessInputSender::Stop() {
    started_ = false;
    VLOG(1) << "InProcessInputSender stopped.";
}

}  // namespace goldfish::videobridge
