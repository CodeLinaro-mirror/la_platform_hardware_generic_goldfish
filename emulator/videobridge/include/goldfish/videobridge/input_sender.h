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
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "absl/status/status.h"

#include "emulator_controller.pb.h"
#include "input/input_service.pb.h"

namespace goldfish::videobridge {

using ::android::emulation::control::InputEvent;
using V2InputEvent = ::android::emulation::v2::input::InputEvent;

/**
 * @brief Labels representing the different type of WebRTC data channels
 * used for forwarding inputs to the emulator.
 */
enum class DataChannelLabel : uint8_t { kInput, kInputV2, kAdb };

inline const char* AsString(DataChannelLabel label) {
    switch (label) {
    case DataChannelLabel::kInput:
        return "input";
    case DataChannelLabel::kInputV2:
        return "input_v2";
    case DataChannelLabel::kAdb:
        return "adb";
    }
    return "unknown";
}

inline std::optional<DataChannelLabel> ParseDataChannelLabel(std::string_view label) {
    if (label == "input") {
        return DataChannelLabel::kInput;
    }
    if (label == "input_v2") {
        return DataChannelLabel::kInputV2;
    }
    if (label == "adb") {
        return DataChannelLabel::kAdb;
    }
    return std::nullopt;
}

/**
 * @class InputSender
 * @brief Abstracts the transport layer for injection of input events into the emulator.
 *
 * This interface decouples WebRTC data channel message parsing from the specific
 * mechanism (such as gRPC client streams or direct in-process QEMU calls) used to
 * write events to the virtual device.
 */
class InputSender {
  public:
    virtual ~InputSender() = default;

    /**
     * @brief Starts the underlying input transport channel (e.g. opens a gRPC stream).
     * Called when the WebRTC data channel has successfully connected.
     *
     * @return absl::Status indicating whether the transport channel was successfully started.
     */
    virtual absl::Status Start() = 0;

    /**
     * @brief Forwards an input event to the emulator.
     *
     * @param event The parsed protobuf InputEvent to be injected.
     */
    virtual void SendEvent(const InputEvent& event) = 0;

    /**
     * @brief Forwards an AEMU v2 heterogeneous input event to the emulator.
     *
     * @param event The parsed v2 protobuf InputEvent to be injected.
     */
    virtual void SendV2Event(const V2InputEvent& /*event*/) {}

    /**
     * @brief Stops the underlying input transport channel and releases resources.
     * Called when the WebRTC data channel disconnects or teardown occurs.
     */
    virtual void Stop() = 0;
};

using InputSenderFactory = std::function<std::unique_ptr<InputSender>(DataChannelLabel)>;

}  // namespace goldfish::videobridge
