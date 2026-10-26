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

#include <stdint.h>

#include "goldfish/legacy/common/c/device_env.h"
#include "goldfish/legacy/common/c/mmio_core.h"

#ifdef __cplusplus
#include <memory>

#include "goldfish/legacy/common/adapters.h"
#include "goldfish/legacy/common/i_guest_memory.h"
#include "goldfish/legacy/common/i_mmio_device.h"

namespace goldfish::legacy {

class GoldfishPipeDevice final : public IMmioDevice {
  public:
    GoldfishPipeDevice(std::unique_ptr<IGuestMemory> memory, SetIrqCallback set_irq);

    uint32_t Read(uint32_t offset) override;
    void Write(uint32_t offset, uint32_t value) override;
    void Reset() override;

  private:
    std::unique_ptr<IGuestMemory> memory_;
    SetIrqCallback set_irq_;
};

}  // namespace goldfish::legacy

extern "C" {
#endif

struct GoldfishMmioCore GoldfishPipeCoreCreate(const struct GoldfishDeviceEnv* env);

#ifdef __cplusplus
}
#endif
