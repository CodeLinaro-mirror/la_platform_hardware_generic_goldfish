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

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace goldfish::update_check {

// The set of SDK update channels, ordered from most to least stable.
enum class UpdateChannel {
    kStable,
    kBeta,
    kDev,
    kCanary,
    kUnknown,
};

struct SdkVersion {
    int major = 0;
    int minor = 0;
    int micro = 0;
    int64_t build_id = 0;  // from "<package_prefix>;<build_id>", or 0 if no suffix

    auto operator<=>(const SdkVersion&) const = default;
};

struct SdkPackage {
    SdkVersion version;
    UpdateChannel channel = UpdateChannel::kUnknown;

    bool operator==(const SdkPackage&) const = default;

    auto operator<=>(const SdkPackage& other) const { return version <=> other.version; }

    // Parses a 3- or 4-part numeric version string ("major.minor.micro[.patch]")
    // and a positive numeric build_id string into an SdkPackage.
    // Returns std::nullopt for non-release/developer builds (e.g. "standalone-0").
    static std::optional<SdkPackage> Parse(std::string_view version, std::string_view build_id);

    std::string VersionString() const;
    std::string ToString() const;
};

// Parses an SDK repository2-3.xml document and returns the newest <remotePackage>
// matching |package_prefix| that contains an <archive> for (|host_os|, |host_arch|)
// and is in a channel at least as stable as |max_channel| (channel <= max_channel),
// ordered by (major, minor, micro, build_id), or std::nullopt if no matching package exists.
absl::StatusOr<std::optional<SdkPackage>> FindLatestPackage(std::string_view xml,
                                                            std::string_view host_os,
                                                            std::string_view host_arch,
                                                            std::string_view package_prefix,
                                                            UpdateChannel max_channel);

}  // namespace goldfish::update_check
