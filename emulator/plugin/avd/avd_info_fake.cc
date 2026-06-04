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
#include "goldfish/display/QemuMultidisplay/multi_display.h"

namespace goldfish::avd_info {

// Implementation of AvdUniverse constructor for unit tests.
AvdUniverse::AvdUniverse(std::unique_ptr<AvdProperties> props)
        : props_(std::move(props)), sensors_physical_model_(props_->hw_config) {}

// Minimal implementation of AvdUniverse for unit tests.
struct FakeAvdUniverse : public AvdUniverse {
    FakeAvdUniverse() : AvdUniverse(std::make_unique<AvdProperties>()) {}
    async::EventLoop& GetQemuEventLoop() override {
        LOG(FATAL) << "GetQemuEventLoop not implemented";
    }
    metrics::MetricsReporter& GetMetricsReporter() override {
        LOG(FATAL) << "GetMetricsReporter not implemented";
    }
    display::IMultiDisplay& GetMultiDisplay() const override { return *multi_display; }

    std::unique_ptr<display::IMultiDisplay> multi_display;
};

AvdUniverse& GetAvd() {
    static FakeAvdUniverse universe;
    return universe;
}

// These methods were merged into avd_info.cc, so we need fake impls here.
void AvdUniverse::SetActiveMultiDisplayDevice(
        std::shared_ptr<devices::multidisplay::MultiDisplayDevice> device) {
    absl::MutexLock lock(&device_mutex_);
    active_multi_display_device_ = device;
}

std::shared_ptr<devices::multidisplay::MultiDisplayDevice>
AvdUniverse::GetActiveMultiDisplayDevice() {
    absl::MutexLock lock(&device_mutex_);
    return active_multi_display_device_;
}

}  // namespace goldfish::avd_info
