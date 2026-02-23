// Copyright 2026 The Android Open Source Project
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

#include <memory>

#include "goldfish/avd_info/avd_info.h"

namespace goldfish::avd_info {

// Implementation of AvdUniverse constructor for unit tests.
AvdUniverse::AvdUniverse(std::unique_ptr<AvdProperties> props)
        : mProps(std::move(props)), mSensorsPhysicalModel(mProps->hw_config) {}

// Minimal implementation of AvdUniverse for unit tests.
struct FakeAvdUniverse : public AvdUniverse {
    FakeAvdUniverse() : AvdUniverse(std::make_unique<AvdProperties>()) {}
};

AvdUniverse& getAvd() {
    static FakeAvdUniverse universe;
    return universe;
}

// These methods were merged into avd_info.cc, so we need fake impls here.
void AvdUniverse::setActiveMultiDisplayDevice(
        std::shared_ptr<devices::multidisplay::MultiDisplayDevice> device) {
    absl::MutexLock lock(&mDeviceMutex);
    mActiveMultiDisplayDevice = device;
}

std::shared_ptr<devices::multidisplay::MultiDisplayDevice>
AvdUniverse::getActiveMultiDisplayDevice() {
    absl::MutexLock lock(&mDeviceMutex);
    return mActiveMultiDisplayDevice;
}

}  // namespace goldfish::avd_info
