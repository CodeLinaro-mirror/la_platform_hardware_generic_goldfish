// Copyright (C) 2025 The Android Open Source Project
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

#include <memory>

#include "absl/status/status.h"

#include "android/goldfish/display/MultiDisplay.h"
#include "goldfish/devices/connector_registry.h"
#include "hardware/generic/goldfish/emulator/android-grpc/services/emulator-controller/proto/emulator_controller.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {

using android::goldfish::IDisplay;
using android::goldfish::IMultiDisplay;
using ::goldfish::devices::ConnectorRegistry;

struct EvDevEvent {
    uint16_t type;
    uint16_t code;
    uint32_t value;

    bool operator==(const EvDevEvent& other) const {
        return type == other.type && code == other.code && value == other.value;
    }
};

template <typename T>
class EventSender {
  public:
    EventSender(IMultiDisplay* multidisplay) : mMultiDisplay(multidisplay) {}
    virtual ~EventSender() = default;

    // Sends the current event to the emulator over the UI thread.
    absl::Status send(const T& event) {
        auto screen = mMultiDisplay->getDisplay(event.display());
        if (!screen.ok()) {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Invalid display: %d", event.display()));
        }
        auto display = screen->lock();
        if (!display) {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Invalid display: %d", event.display()));
        }

        return doSend(*display.get(), event);
    }

  protected:
    virtual absl::Status doSend(IDisplay& display, const T& event) = 0;

    IMultiDisplay* mMultiDisplay;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
