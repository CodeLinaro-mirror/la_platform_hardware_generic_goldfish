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

#include "goldfish/update_check/repository_manifest.h"

#include <tinyxml2.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace goldfish::update_check {
namespace {

constexpr std::string_view kRepositoryXmlNamespace =
        "http://schemas.android.com/sdk/android/repo/repository2/03";

std::pair<std::string_view, std::string_view> SplitXmlTagName(const char* raw_name) {
    if (raw_name == nullptr) {
        return {};
    }
    const std::vector<std::string_view> parts = absl::StrSplit(raw_name, absl::MaxSplits(':', 1));
    return parts.size() == 2 ? std::make_pair(parts[0], parts[1])
                             : std::make_pair(std::string_view{}, parts[0]);
}

std::string_view LocalTagName(const char* raw_name) {
    return SplitXmlTagName(raw_name).second;
}

const tinyxml2::XMLElement* FindChildByLocalName(const tinyxml2::XMLElement* parent,
                                                 std::string_view local_name) {
    if (parent == nullptr) {
        return nullptr;
    }
    for (const auto* child = parent->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (LocalTagName(child->Name()) == local_name) {
            return child;
        }
    }
    return nullptr;
}

std::optional<std::string_view> GetChildText(const tinyxml2::XMLElement* parent,
                                             std::string_view local_name) {
    const auto* child = FindChildByLocalName(parent, local_name);
    if (child == nullptr || child->GetText() == nullptr) {
        return std::nullopt;
    }
    return absl::StripAsciiWhitespace(child->GetText());
}

bool IsDigitsOnly(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (const char c : s) {
        if (!absl::ascii_isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

template <typename IntType>
std::optional<IntType> ParseAsciiUnsigned(std::string_view s, IntType min_value) {
    if (!IsDigitsOnly(s)) {
        return std::nullopt;
    }
    IntType val = 0;
    if (!absl::SimpleAtoi(s, &val) || val < min_value) {
        return std::nullopt;
    }
    return val;
}

std::optional<int> ParseNonNegativeInt(std::string_view s) {
    return ParseAsciiUnsigned<int>(s, 0);
}

std::optional<int64_t> ParsePositiveBuildId(std::string_view s) {
    return ParseAsciiUnsigned<int64_t>(s, 1);
}

std::optional<SdkVersion> ParseSdkVersion(std::string_view major_str, std::string_view minor_str,
                                          std::string_view micro_str, int64_t build_id) {
    const auto major = ParseNonNegativeInt(major_str);
    const auto minor = ParseNonNegativeInt(minor_str);
    const auto micro = ParseNonNegativeInt(micro_str);
    if (!major.has_value() || !minor.has_value() || !micro.has_value()) {
        return std::nullopt;
    }
    return SdkVersion{
        .major = *major,
        .minor = *minor,
        .micro = *micro,
        .build_id = build_id,
    };
}

absl::Status ValidateRootElement(const tinyxml2::XMLElement* root) {
    if (root == nullptr) {
        return absl::InvalidArgumentError(
                "Repository XML is missing a root <sdk-repository> element");
    }
    const auto [prefix, local_name] = SplitXmlTagName(root->Name());
    if (local_name != "sdk-repository") {
        return absl::InvalidArgumentError(absl::StrCat(
                "Repository XML root element must be <sdk-repository>, got <", root->Name(), ">"));
    }
    const std::string attr_name = prefix.empty() ? "xmlns" : absl::StrCat("xmlns:", prefix);
    const char* ns = root->Attribute(attr_name.c_str());
    if (ns == nullptr || std::string_view(ns) != kRepositoryXmlNamespace) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Repository XML root element <", root->Name(), "> has unsupported ", attr_name,
                "='", (ns != nullptr ? ns : ""), "', expected '", kRepositoryXmlNamespace, "'"));
    }
    return absl::OkStatus();
}

bool PackageSupportsHost(const tinyxml2::XMLElement* remote_pkg, std::string_view target_os,
                         std::string_view target_arch) {
    const auto* archives = FindChildByLocalName(remote_pkg, "archives");
    if (archives == nullptr) {
        return false;
    }
    for (const auto* archive = archives->FirstChildElement(); archive != nullptr;
         archive = archive->NextSiblingElement()) {
        if (LocalTagName(archive->Name()) != "archive") {
            continue;
        }
        const auto host_os = GetChildText(archive, "host-os");
        const auto host_arch = GetChildText(archive, "host-arch");
        if (host_os.has_value() && host_arch.has_value() && *host_os == target_os &&
            *host_arch == target_arch) {
            return true;
        }
    }
    return false;
}

std::optional<int64_t> MatchPackagePathBuildId(std::string_view path,
                                               std::string_view package_prefix) {
    if (!absl::StartsWith(path, package_prefix)) {
        return std::nullopt;
    }
    std::string_view suffix = path.substr(package_prefix.size());
    if (absl::EndsWith(package_prefix, ";")) {
        return ParsePositiveBuildId(suffix);
    }
    if (suffix.empty()) {
        return 0;
    }
    if (absl::StartsWith(suffix, ";")) {
        return ParsePositiveBuildId(suffix.substr(1));
    }
    return std::nullopt;
}

UpdateChannel ParseUpdateChannelName(std::string_view name) {
    if (name == "stable") {
        return UpdateChannel::kStable;
    }
    if (name == "beta") {
        return UpdateChannel::kBeta;
    }
    if (name == "dev") {
        return UpdateChannel::kDev;
    }
    if (name == "canary") {
        return UpdateChannel::kCanary;
    }
    return UpdateChannel::kUnknown;
}

}  // namespace

std::optional<SdkPackage> SdkPackage::Parse(std::string_view version, std::string_view build_id) {
    const auto parsed_build_id = ParsePositiveBuildId(absl::StripAsciiWhitespace(build_id));
    if (!parsed_build_id.has_value()) {
        return std::nullopt;
    }

    const std::vector<std::string_view> parts =
            absl::StrSplit(absl::StripAsciiWhitespace(version), '.');
    if (parts.size() < 3 || parts.size() > 4) {
        return std::nullopt;
    }

    const auto parsed_version = ParseSdkVersion(parts[0], parts[1], parts[2], *parsed_build_id);
    if (!parsed_version.has_value()) {
        return std::nullopt;
    }
    if (parts.size() == 4 && !ParseNonNegativeInt(parts[3]).has_value()) {
        return std::nullopt;
    }
    return SdkPackage{.version = *parsed_version};
}

std::string SdkPackage::VersionString() const {
    return absl::StrFormat("%d.%d.%d", version.major, version.minor, version.micro);
}

std::string SdkPackage::ToString() const {
    if (version.build_id > 0) {
        return absl::StrCat(VersionString(), " (build ", version.build_id, ")");
    }
    return VersionString();
}

absl::StatusOr<std::optional<SdkPackage>> FindLatestPackage(std::string_view xml,
                                                            std::string_view host_os,
                                                            std::string_view host_arch,
                                                            std::string_view package_prefix,
                                                            UpdateChannel max_channel) {
    if (xml.empty()) {
        return absl::InvalidArgumentError("Empty repository XML manifest");
    }
    if (package_prefix.empty()) {
        return absl::InvalidArgumentError("Package prefix must not be empty");
    }

    tinyxml2::XMLDocument doc;
    const tinyxml2::XMLError err = doc.Parse(xml.data(), xml.size());
    if (err != tinyxml2::XML_SUCCESS) {
        return absl::InvalidArgumentError(
                absl::StrCat("Failed to parse repository XML: ", doc.ErrorStr()));
    }

    const tinyxml2::XMLElement* root = doc.RootElement();
    if (absl::Status s = ValidateRootElement(root); !s.ok()) {
        return s;
    }

    std::unordered_map<std::string, UpdateChannel> channels;
    for (const auto* node = root->FirstChildElement(); node != nullptr;
         node = node->NextSiblingElement()) {
        if (LocalTagName(node->Name()) != "channel") {
            continue;
        }
        const char* id = node->Attribute("id");
        const char* text = node->GetText();
        if (id != nullptr && text != nullptr) {
            const UpdateChannel channel = ParseUpdateChannelName(absl::StripAsciiWhitespace(text));
            if (channel != UpdateChannel::kUnknown) {
                channels[id] = channel;
            }
        }
    }

    const UpdateChannel effective_max_channel =
            (max_channel == UpdateChannel::kUnknown) ? UpdateChannel::kStable : max_channel;

    std::optional<SdkPackage> latest;

    for (const auto* node = root->FirstChildElement(); node != nullptr;
         node = node->NextSiblingElement()) {
        if (LocalTagName(node->Name()) != "remotePackage") {
            continue;
        }
        const char* path_attr = node->Attribute("path");
        if (path_attr == nullptr) {
            continue;
        }
        const auto build_id = MatchPackagePathBuildId(path_attr, package_prefix);
        if (!build_id.has_value()) {
            continue;
        }

        const auto* revision = FindChildByLocalName(node, "revision");
        if (revision == nullptr) {
            continue;
        }
        const auto major_str = GetChildText(revision, "major");
        const auto minor_str = GetChildText(revision, "minor");
        const auto micro_str = GetChildText(revision, "micro");
        if (!major_str.has_value() || !minor_str.has_value() || !micro_str.has_value()) {
            continue;
        }
        const auto parsed_version = ParseSdkVersion(*major_str, *minor_str, *micro_str, *build_id);
        if (!parsed_version.has_value()) {
            continue;
        }

        if (!PackageSupportsHost(node, host_os, host_arch)) {
            continue;
        }

        UpdateChannel channel = UpdateChannel::kCanary;
        if (const auto* channel_ref = FindChildByLocalName(node, "channelRef")) {
            if (const char* ref = channel_ref->Attribute("ref")) {
                if (auto it = channels.find(ref); it != channels.end()) {
                    channel = it->second;
                }
            }
        }

        if (channel > effective_max_channel) {
            continue;
        }

        SdkPackage candidate{
            .version = *parsed_version,
            .channel = channel,
        };
        if (!latest.has_value() || candidate > *latest) {
            latest = candidate;
        }
    }

    return latest;
}

}  // namespace goldfish::update_check
