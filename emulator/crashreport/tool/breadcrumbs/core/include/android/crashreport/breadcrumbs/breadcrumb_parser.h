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
// limitations under the License.#pragma once
#include <cstdint>
#include <vector>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Logic for extracting gRPC breadcrumbs from minidumps.
 *
 * The BreadcrumbParser interface with the CircularMessageLog library to correctly
 * handle the wrap-around logic and binary reconstruction of the in-memory flight
 * recorder buffer.
 */
class BreadcrumbParser {
  public:
    /**
     * @brief Parses a raw memory buffer into a sequence of GrpcBreadcrumb protos.
     *
     * Validates the buffer header (magic, head, tail) and iterates through the
     * circular buffer to produce a chronologically sorted sequence of events.
     *
     * @param buffer The raw memory buffer extracted from the minidump annotation.
     * @return A vector of entries ordered from oldest to newest.
     */
    static std::vector<Breadcrumb> Parse(const std::vector<uint8_t>& buffer);
};

}  // namespace android::crashreport::breadcrumbs
