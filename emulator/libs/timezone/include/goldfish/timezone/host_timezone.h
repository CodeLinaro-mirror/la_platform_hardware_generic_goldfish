/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace goldfish::timezone {

// Returns true if |name| has the form of a zoneinfo timezone name, i.e.
// Area/Location or Area/Location/SubLocation (e.g. "Europe/Paris" or
// "America/Argentina/Buenos_Aires").
bool IsZoneinfoName(std::string_view name);

// Returns the zoneinfo name of the host timezone (e.g. "Asia/Tokyo"), or
// std::nullopt if it cannot be determined.
std::optional<std::string> GetHostTimezone();

// Returns the timezone to report to the guest: |requested| (e.g. the value of
// the -timezone option, or nullptr) if it is a zoneinfo name, otherwise
// |host_timezone|.
std::optional<std::string> GetGuestTimezone(const char* requested,
                                            std::optional<std::string> host_timezone);

// Implementation details, exposed for testing.
namespace internal {

// A Windows timezone registry key name and its default zoneinfo name.
struct WindowsZone {
    std::string_view key_name;
    std::string_view zoneinfo_name;
};

// Returns the Windows timezones known to Win32TimezoneToZoneinfo().
std::span<const WindowsZone> WindowsZones();

// Returns the zoneinfo name of a Windows timezone. It is looked up by its
// registry key name first (e.g. "Tokyo Standard Time"), which is never
// localized, then by the name returned by strftime("%Z"), which is localized on
// non-English Windows installations.
std::optional<std::string_view> Win32TimezoneToZoneinfo(std::string_view key_name,
                                                        std::string_view standard_name);

// Returns the zoneinfo name in the path of the file that /etc/localtime links
// to, e.g. "America/Los_Angeles" for "/usr/share/zoneinfo/America/Los_Angeles".
std::optional<std::string> ZoneinfoNameFromLink(std::string_view link_target);

// Returns the name of the file under |zoneinfo_dir| with the same content as
// |localtime|, for hosts where /etc/localtime is a copy instead of a symlink.
std::optional<std::string> FindZoneinfoNameByContent(const std::filesystem::path& localtime,
                                                     const std::filesystem::path& zoneinfo_dir);

}  // namespace internal
}  // namespace goldfish::timezone
