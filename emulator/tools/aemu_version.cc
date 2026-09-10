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

#include "goldfish/tools/aemu_version.h"

#include <string>
#include <string_view>

namespace goldfish::version {

// Defined in aemu_linkstamp.cc (compiled per-binary at link time).
extern const char kStampedBuildId[];

std::string_view GetEmulatorBuildId() {
    if (kStampedBuildId[0] != '\0') {
        return kStampedBuildId;
    }
    return "developer";
}

std::string_view GetEmulatorFullVersion() {
    static const std::string* const kFullVersion = []() {
        std::string_view build_id = GetEmulatorBuildId();
        return new std::string(std::string(VERSION) + "-" + std::string(build_id));
    }();
    return *kFullVersion;
}

}  // namespace goldfish::version
