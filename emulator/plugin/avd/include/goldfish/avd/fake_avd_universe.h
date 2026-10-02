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

#pragma once

#include "absl/status/status.h"

#include "goldfish/avd/avd_universe.h"
#include "goldfish/metrics/metrics_reporter.h"

namespace goldfish::avd {

// Minimal implementation of AvdUniverse for unit tests.
// Registers itself as the global AvdUniverse on construction and clears it on destruction.
class FakeAvdUniverse : public AvdUniverse {
  public:
    FakeAvdUniverse();
    ~FakeAvdUniverse() override;

    async::EventLoop& GetQemuEventLoop() override;
    metrics::MetricsReporter& GetMetricsReporter() override;
    display::IMultiDisplay& GetMultiDisplay() const override;

    absl::Status OnSave(archive::IWriter&) const override;
    absl::Status OnLoad(archive::IReader&) override;

  private:
    metrics::MetricsReporter metrics_reporter_;
};

}  // namespace goldfish::avd
