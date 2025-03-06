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
#include "android/emulation/control/input/AndroidEventSender.h"

#include "absl/status/status.h"
namespace android {
namespace emulation {
namespace control {

absl::Status AndroidEventSender::doSend(IDisplay& display, const AndroidEvent& event) {
    display.sendEvDevEvent(event.type(), event.code(), event.value());
    return absl::OkStatus();
};

}  // namespace control
}  // namespace emulation
}  // namespace android
