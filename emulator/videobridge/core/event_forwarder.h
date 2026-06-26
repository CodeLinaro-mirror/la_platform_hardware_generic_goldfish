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
// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/data_channel_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include <cstdint>
#include <memory>

#include "goldfish/videobridge/input_sender.h"

namespace goldfish::videobridge {

/**
 * @brief EventForwarder listens on a single WebRTC data channel, converts
 * parsed events to gRPC InputEvents, and writes them directly to the emulator.
 */
class EventForwarder : public ::webrtc::DataChannelObserver {
  public:
    EventForwarder(webrtc::scoped_refptr<::webrtc::DataChannelInterface> channel,
                   DataChannelLabel label, std::unique_ptr<InputSender> sender);
    ~EventForwarder() override;

    // DataChannelObserver implementation
    void OnStateChange() override;
    void OnMessage(const ::webrtc::DataBuffer& buffer) override;

  private:
    webrtc::scoped_refptr<::webrtc::DataChannelInterface> channel_;
    DataChannelLabel label_;
    std::unique_ptr<InputSender> sender_;
};

}  // namespace goldfish::videobridge
