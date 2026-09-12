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

#include "goldfish/videobridge/session_id.h"

#include <cstdint>
#include <string>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"

namespace goldfish::videobridge {

std::string GenerateSessionId(uint64_t high, uint64_t low) {
    // RFC 4122 section 4.4: the 4 bit version field is set to 0b0100 and the 2 most significant
    // bits of clock_seq_hi_and_reserved are set to 0b10. The remaining 122 bits are random.
    const auto time_low = static_cast<uint32_t>(high >> 32);
    const auto time_mid = static_cast<uint16_t>(high >> 16);
    const auto time_hi_and_version = static_cast<uint16_t>((high & 0x0FFF) | 0x4000);
    const auto clock_seq = static_cast<uint16_t>(((low >> 48) & 0x3FFF) | 0x8000);
    const uint64_t node = low & 0xFFFFFFFFFFFFULL;

    return absl::StrFormat("%08x-%04x-%04x-%04x-%012x", time_low, time_mid, time_hi_and_version,
                           clock_seq, node);
}

std::string GenerateSessionId() {
    thread_local absl::BitGen bitgen;
    const uint64_t high = absl::Uniform<uint64_t>(bitgen);
    const uint64_t low = absl::Uniform<uint64_t>(bitgen);
    return GenerateSessionId(high, low);
}

}  // namespace goldfish::videobridge
