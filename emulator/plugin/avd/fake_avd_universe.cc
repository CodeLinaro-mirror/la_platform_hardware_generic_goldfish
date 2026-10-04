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

#include "goldfish/avd/fake_avd_universe.h"

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "goldfish/avd/avd_info.h"
#include "goldfish/avd/avd_properties.h"

namespace goldfish::avd {

FakeAvdUniverse::FakeAvdUniverse() : AvdUniverse(std::make_unique<AvdProperties>()) {
    SetGlobalAvdUniverse(this);
}

FakeAvdUniverse::~FakeAvdUniverse() {
    SetGlobalAvdUniverse(nullptr);
}

async::EventLoop& FakeAvdUniverse::GetQemuEventLoop() {
    LOG(FATAL) << "GetQemuEventLoop not implemented";
}

metrics::MetricsReporter& FakeAvdUniverse::GetMetricsReporter() {
    LOG(FATAL) << "GetMetricsReporter not implemented";
}

display::IMultiDisplay& FakeAvdUniverse::GetMultiDisplay() const {
    LOG(FATAL) << "GetMultiDisplay not implemented";
}

absl::Status FakeAvdUniverse::OnSave(archive::IWriter&) const {
    return absl::UnimplementedError(__func__);
}

absl::Status FakeAvdUniverse::OnLoad(archive::IReader&) {
    return absl::UnimplementedError(__func__);
}

}  // namespace goldfish::avd
