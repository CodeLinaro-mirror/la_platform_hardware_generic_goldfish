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
#include <string>

namespace goldfish::videobridge {

/**
 * @brief Formats an RFC 4122 version 4 (random) UUID from 128 bits of entropy.
 *
 * @param high The upper 64 bits of entropy.
 * @param low The lower 64 bits of entropy.
 * @return A 36 character canonical UUID string, e.g. "f81d4fae-7dec-41d0-a765-00a0c91e6bf6".
 */
std::string GenerateSessionId(uint64_t high, uint64_t low);

/**
 * @brief Generates an RFC 4122 version 4 (random) UUID identifying a signaling session.
 *
 * @return A 36 character canonical UUID string, e.g. "f81d4fae-7dec-41d0-a765-00a0c91e6bf6".
 */
std::string GenerateSessionId();

}  // namespace goldfish::videobridge
