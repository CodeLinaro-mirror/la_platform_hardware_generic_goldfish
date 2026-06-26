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

#include "event_forwarder.h"

#include "absl/log/log.h"

#include "participant.h"

namespace goldfish::videobridge {

using ::android::emulation::control::InputEvent;
using ::android::emulation::control::KeyboardEvent;
using ::android::emulation::control::MouseEvent;
using ::android::emulation::control::TouchEvent;

EventForwarder::EventForwarder(webrtc::scoped_refptr<::webrtc::DataChannelInterface> channel,
                               DataChannelLabel label, std::unique_ptr<InputSender> sender)
        : channel_(std::move(channel)), label_(label), sender_(std::move(sender)) {
    channel_->RegisterObserver(this);
    // If the channel is already open, trigger initialization immediately.
    OnStateChange();
}

EventForwarder::~EventForwarder() {
    channel_->UnregisterObserver();
}

void EventForwarder::OnStateChange() {
    if (channel_->state() == ::webrtc::DataChannelInterface::kOpen) {
        VLOG(1) << "WebRTC DataChannel state changed to OPEN for label: " << AsString(label_);
        if (auto status = sender_->Start(); !status.ok()) {
            LOG(ERROR) << "Failed to start input sender for data channel '" << AsString(label_)
                       << "': " << status;
            channel_->Close();
        }
    } else if (channel_->state() == ::webrtc::DataChannelInterface::kClosing ||
               channel_->state() == ::webrtc::DataChannelInterface::kClosed) {
        VLOG(1) << "WebRTC DataChannel state changed to CLOSING/CLOSED for label: "
                << AsString(label_);
        sender_->Stop();
    }
}

void EventForwarder::OnMessage(const ::webrtc::DataBuffer& buffer) {
    InputEvent input_event;

    if (label_ == DataChannelLabel::kInput) {
        if (!input_event.ParseFromArray(buffer.data.data(), static_cast<int>(buffer.size()))) {
            LOG(ERROR) << "Failed to deserialize InputEvent protobuf from data channel '"
                       << AsString(label_) << "'";
            return;
        }
    } else if (label_ == DataChannelLabel::kAdb) {
        LOG(WARNING) << "Received ADB command over WebRTC data channel for '" << AsString(label_)
                     << "', but ADB over WebRTC data channels is deprecated.";
        return;
    } else {
        LOG(WARNING) << "Ignoring message received on unhandled/unsupported data channel type.";
        return;
    }

    sender_->SendEvent(input_event);
}

}  // namespace goldfish::videobridge
