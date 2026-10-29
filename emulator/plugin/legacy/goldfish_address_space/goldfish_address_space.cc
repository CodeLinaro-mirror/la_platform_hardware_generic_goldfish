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

#include "goldfish_address_space.h"

#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/legacy/common/adapters.h"

namespace goldfish::legacy {

GoldfishAddressSpaceDevice::GoldfishAddressSpaceDevice(std::unique_ptr<IGuestMemory> memory,
                                                       std::unique_ptr<IBar1Mapper> bar1,
                                                       SetIrqCallback set_irq)
        : memory_(std::move(memory)), bar1_(std::move(bar1)), set_irq_(std::move(set_irq)) {
    DCHECK(memory_ != nullptr);
    DCHECK(bar1_ != nullptr);
    DCHECK(set_irq_ != nullptr);
}

uint32_t GoldfishAddressSpaceDevice::Read(uint32_t offset) {
    LOG(QFATAL) << "GoldfishAddressSpaceDevice::Read unimplemented (offset=" << std::hex << offset
                << ")";
    return 0;
}

void GoldfishAddressSpaceDevice::Write(uint32_t offset, uint32_t value) {
    LOG(QFATAL) << "GoldfishAddressSpaceDevice::Write unimplemented (offset=" << std::hex << offset
                << ", value=" << value << ")";
}

void GoldfishAddressSpaceDevice::Reset() {
    set_irq_(false);
}

}  // namespace goldfish::legacy

extern "C" GoldfishMmioCore GoldfishAddressSpaceCoreCreate(const GoldfishDeviceEnv* env) {
    DCHECK(env != nullptr);
    auto dev = std::make_unique<goldfish::legacy::GoldfishAddressSpaceDevice>(
            goldfish::legacy::WrapGuestMemory(env->memory), goldfish::legacy::WrapBar1(env->bar1),
            goldfish::legacy::WrapIrq(env->irq));
    return goldfish::legacy::ToMmioCore(std::move(dev));
}

extern "C" uint64_t GoldfishAddressSpaceHostPageSize(void) {
#if defined(__APPLE__) && defined(__arm64__)
    return 16384ULL;
#else
    return 4096ULL;
#endif
}
