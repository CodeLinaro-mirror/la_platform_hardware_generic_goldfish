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

#include "goldfish/timezone/host_timezone.h"

#include <algorithm>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"

#include "goldfish/file/file.h"
#include "goldfish/file/storage_capacity.h"

#ifdef _WIN32
#include <windows.h>

#include <ctime>

#include "absl/log/log.h"

#include "android/base/win32_unicode_string.h"
#else
#include "absl/strings/strip.h"

#include "android/base/system.h"
#endif

namespace goldfish::timezone {
namespace {

namespace fs = std::filesystem;

// The default (territory "001") zone of every Windows timezone, sorted by key
// name. Generated from CLDR 48.2 common/supplemental/windowsZones.xml.
constexpr internal::WindowsZone kWindowsZones[] = {
    {"AUS Central Standard Time", "Australia/Darwin"},
    {"AUS Eastern Standard Time", "Australia/Sydney"},
    {"Afghanistan Standard Time", "Asia/Kabul"},
    {"Alaskan Standard Time", "America/Anchorage"},
    {"Aleutian Standard Time", "America/Adak"},
    {"Altai Standard Time", "Asia/Barnaul"},
    {"Arab Standard Time", "Asia/Riyadh"},
    {"Arabian Standard Time", "Asia/Dubai"},
    {"Arabic Standard Time", "Asia/Baghdad"},
    {"Argentina Standard Time", "America/Buenos_Aires"},
    {"Astrakhan Standard Time", "Europe/Astrakhan"},
    {"Atlantic Standard Time", "America/Halifax"},
    {"Aus Central W. Standard Time", "Australia/Eucla"},
    {"Azerbaijan Standard Time", "Asia/Baku"},
    {"Azores Standard Time", "Atlantic/Azores"},
    {"Bahia Standard Time", "America/Bahia"},
    {"Bangladesh Standard Time", "Asia/Dhaka"},
    {"Belarus Standard Time", "Europe/Minsk"},
    {"Bougainville Standard Time", "Pacific/Bougainville"},
    {"Canada Central Standard Time", "America/Regina"},
    {"Cape Verde Standard Time", "Atlantic/Cape_Verde"},
    {"Caucasus Standard Time", "Asia/Yerevan"},
    {"Cen. Australia Standard Time", "Australia/Adelaide"},
    {"Central America Standard Time", "America/Guatemala"},
    {"Central Asia Standard Time", "Asia/Bishkek"},
    {"Central Brazilian Standard Time", "America/Cuiaba"},
    {"Central Europe Standard Time", "Europe/Budapest"},
    {"Central European Standard Time", "Europe/Warsaw"},
    {"Central Pacific Standard Time", "Pacific/Guadalcanal"},
    {"Central Standard Time", "America/Chicago"},
    {"Central Standard Time (Mexico)", "America/Mexico_City"},
    {"Chatham Islands Standard Time", "Pacific/Chatham"},
    {"China Standard Time", "Asia/Shanghai"},
    {"Cuba Standard Time", "America/Havana"},
    {"Dateline Standard Time", "Etc/GMT+12"},
    {"E. Africa Standard Time", "Africa/Nairobi"},
    {"E. Australia Standard Time", "Australia/Brisbane"},
    {"E. Europe Standard Time", "Europe/Chisinau"},
    {"E. South America Standard Time", "America/Sao_Paulo"},
    {"Easter Island Standard Time", "Pacific/Easter"},
    {"Eastern Standard Time", "America/New_York"},
    {"Eastern Standard Time (Mexico)", "America/Cancun"},
    {"Egypt Standard Time", "Africa/Cairo"},
    {"Ekaterinburg Standard Time", "Asia/Yekaterinburg"},
    {"FLE Standard Time", "Europe/Kiev"},
    {"Fiji Standard Time", "Pacific/Fiji"},
    {"GMT Standard Time", "Europe/London"},
    {"GTB Standard Time", "Europe/Bucharest"},
    {"Georgian Standard Time", "Asia/Tbilisi"},
    {"Greenland Standard Time", "America/Godthab"},
    {"Greenwich Standard Time", "Atlantic/Reykjavik"},
    {"Haiti Standard Time", "America/Port-au-Prince"},
    {"Hawaiian Standard Time", "Pacific/Honolulu"},
    {"India Standard Time", "Asia/Calcutta"},
    {"Iran Standard Time", "Asia/Tehran"},
    {"Israel Standard Time", "Asia/Jerusalem"},
    {"Jordan Standard Time", "Asia/Amman"},
    {"Kaliningrad Standard Time", "Europe/Kaliningrad"},
    {"Korea Standard Time", "Asia/Seoul"},
    {"Libya Standard Time", "Africa/Tripoli"},
    {"Line Islands Standard Time", "Pacific/Kiritimati"},
    {"Lord Howe Standard Time", "Australia/Lord_Howe"},
    {"Magadan Standard Time", "Asia/Magadan"},
    {"Magallanes Standard Time", "America/Punta_Arenas"},
    {"Marquesas Standard Time", "Pacific/Marquesas"},
    {"Mauritius Standard Time", "Indian/Mauritius"},
    {"Middle East Standard Time", "Asia/Beirut"},
    {"Montevideo Standard Time", "America/Montevideo"},
    {"Morocco Standard Time", "Africa/Casablanca"},
    {"Mountain Standard Time", "America/Denver"},
    {"Mountain Standard Time (Mexico)", "America/Mazatlan"},
    {"Myanmar Standard Time", "Asia/Rangoon"},
    {"N. Central Asia Standard Time", "Asia/Novosibirsk"},
    {"Namibia Standard Time", "Africa/Windhoek"},
    {"Nepal Standard Time", "Asia/Katmandu"},
    {"New Zealand Standard Time", "Pacific/Auckland"},
    {"Newfoundland Standard Time", "America/St_Johns"},
    {"Norfolk Standard Time", "Pacific/Norfolk"},
    {"North Asia East Standard Time", "Asia/Irkutsk"},
    {"North Asia Standard Time", "Asia/Krasnoyarsk"},
    {"North Korea Standard Time", "Asia/Pyongyang"},
    {"Omsk Standard Time", "Asia/Omsk"},
    {"Pacific SA Standard Time", "America/Santiago"},
    {"Pacific Standard Time", "America/Los_Angeles"},
    {"Pacific Standard Time (Mexico)", "America/Tijuana"},
    {"Pakistan Standard Time", "Asia/Karachi"},
    {"Paraguay Standard Time", "America/Asuncion"},
    {"Qyzylorda Standard Time", "Asia/Qyzylorda"},
    {"Romance Standard Time", "Europe/Paris"},
    {"Russia Time Zone 10", "Asia/Srednekolymsk"},
    {"Russia Time Zone 11", "Asia/Kamchatka"},
    {"Russia Time Zone 3", "Europe/Samara"},
    {"Russian Standard Time", "Europe/Moscow"},
    {"SA Eastern Standard Time", "America/Cayenne"},
    {"SA Pacific Standard Time", "America/Bogota"},
    {"SA Western Standard Time", "America/La_Paz"},
    {"SE Asia Standard Time", "Asia/Bangkok"},
    {"Saint Pierre Standard Time", "America/Miquelon"},
    {"Sakhalin Standard Time", "Asia/Sakhalin"},
    {"Samoa Standard Time", "Pacific/Apia"},
    {"Sao Tome Standard Time", "Africa/Sao_Tome"},
    {"Saratov Standard Time", "Europe/Saratov"},
    {"Singapore Standard Time", "Asia/Singapore"},
    {"South Africa Standard Time", "Africa/Johannesburg"},
    {"South Sudan Standard Time", "Africa/Juba"},
    {"Sri Lanka Standard Time", "Asia/Colombo"},
    {"Sudan Standard Time", "Africa/Khartoum"},
    {"Syria Standard Time", "Asia/Damascus"},
    {"Taipei Standard Time", "Asia/Taipei"},
    {"Tasmania Standard Time", "Australia/Hobart"},
    {"Tocantins Standard Time", "America/Araguaina"},
    {"Tokyo Standard Time", "Asia/Tokyo"},
    {"Tomsk Standard Time", "Asia/Tomsk"},
    {"Tonga Standard Time", "Pacific/Tongatapu"},
    {"Transbaikal Standard Time", "Asia/Chita"},
    {"Turkey Standard Time", "Europe/Istanbul"},
    {"Turks And Caicos Standard Time", "America/Grand_Turk"},
    {"US Eastern Standard Time", "America/Indianapolis"},
    {"US Mountain Standard Time", "America/Phoenix"},
    {"UTC", "Etc/UTC"},
    {"UTC+12", "Etc/GMT-12"},
    {"UTC+13", "Etc/GMT-13"},
    {"UTC-02", "Etc/GMT+2"},
    {"UTC-08", "Etc/GMT+8"},
    {"UTC-09", "Etc/GMT+9"},
    {"UTC-11", "Etc/GMT+11"},
    {"Ulaanbaatar Standard Time", "Asia/Ulaanbaatar"},
    {"Venezuela Standard Time", "America/Caracas"},
    {"Vladivostok Standard Time", "Asia/Vladivostok"},
    {"Volgograd Standard Time", "Europe/Volgograd"},
    {"W. Australia Standard Time", "Australia/Perth"},
    {"W. Central Africa Standard Time", "Africa/Lagos"},
    {"W. Europe Standard Time", "Europe/Berlin"},
    {"W. Mongolia Standard Time", "Asia/Hovd"},
    {"West Asia Standard Time", "Asia/Tashkent"},
    {"West Bank Standard Time", "Asia/Hebron"},
    {"West Pacific Standard Time", "Pacific/Port_Moresby"},
    {"Yakutsk Standard Time", "Asia/Yakutsk"},
    {"Yukon Standard Time", "America/Whitehorse"},
};

std::optional<std::string_view> LookUpWindowsZone(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    for (const internal::WindowsZone& zone : kWindowsZones) {
        if (zone.key_name == name) {
            return zone.zoneinfo_name;
        }
    }
    return std::nullopt;
}

// Directories of the zoneinfo database that hold copies of the zones under
// another name.
bool IsZoneinfoCopyDir(std::string_view name) {
    return name == "posix" || name == "right";
}

// Appends the paths, relative to |root|, of the zone files under
// |root| / |relative_dir|, which is |depth| directories below |root|: the
// regular files at depth 1 (Area/Location) and 2 (Area/Location/SubLocation).
// Symlinks are skipped: they are aliases of other zones.
void CollectZoneFiles(const fs::path& root, const fs::path& relative_dir, int depth,
                      std::vector<fs::path>* zone_files) {
    for (const fs::path& entry : android::base::file::scan_dir(root / relative_dir)) {
        const std::string filename = entry.string();
        if (filename.empty() || filename.front() == '.') {
            continue;
        }
        const fs::path relative_path = relative_dir / filename;
        const fs::path path = root / relative_path;
        if (android::base::file::is_link(path)) {
            continue;
        }
        if (android::base::file::is_dir(path)) {
            if (depth < 2 && !(depth == 0 && IsZoneinfoCopyDir(filename))) {
                CollectZoneFiles(root, relative_path, depth + 1, zone_files);
            }
        } else if (depth > 0 && android::base::file::is_file(path)) {
            zone_files->push_back(relative_path);
        }
    }
}

#ifdef _WIN32

std::optional<std::string> DetectHostTimezone() {
    // Prefer the registry key name of the timezone (e.g. "Tokyo Standard
    // Time"): unlike the name returned by strftime("%Z"), it is not localized.
    std::string key_name;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
    if (GetDynamicTimeZoneInformation(&dtzi) != TIME_ZONE_ID_INVALID) {
        key_name = android::base::Win32UnicodeString::convertToUtf8(dtzi.TimeZoneKeyName);
    } else {
        LOG(WARNING) << "GetDynamicTimeZoneInformation() failed: error " << GetLastError();
    }

    // Fall back to the name reported by the C runtime. Windows timezones are
    // named after their standard time, so ask for the standard name.
    char standard_name[128] = "";
    const std::time_t now = std::time(nullptr);
    std::tm local_tm = {};
    if (localtime_s(&local_tm, &now) == 0) {
        local_tm.tm_isdst = 0;
        if (std::strftime(standard_name, sizeof(standard_name), "%Z", &local_tm) == 0) {
            standard_name[0] = '\0';
        }
    }

    if (auto zoneinfo_name = internal::Win32TimezoneToZoneinfo(key_name, standard_name)) {
        return std::string(*zoneinfo_name);
    }
    LOG(WARNING) << "Unknown Windows timezone (key name: '" << key_name << "', standard name: '"
                 << standard_name << "')";
    return std::nullopt;
}

#else  // !_WIN32

constexpr char kLocaltimePath[] = "/etc/localtime";
constexpr char kDefaultZoneinfoDir[] = "/usr/share/zoneinfo";

std::optional<std::string> DetectHostTimezone() {
    // The TZ environment variable, e.g. "Asia/Tokyo" or ":Asia/Tokyo".
    const std::string tz = android::base::System::Get()->EnvGet("TZ");
    if (const std::string_view name = absl::StripPrefix(tz, ":"); IsZoneinfoName(name)) {
        return std::string(name);
    }

    // The zoneinfo file that /etc/localtime links to, on most Linux
    // distributions and on macOS.
    const absl::StatusOr<fs::path> localtime_target =
            android::base::file::make_canonical(kLocaltimePath);
    if (localtime_target.ok()) {
        if (auto zoneinfo_name = internal::ZoneinfoNameFromLink(localtime_target->string())) {
            return zoneinfo_name;
        }
    }

    // A zoneinfo file with the same content as /etc/localtime.
    const std::string tzdir = android::base::System::Get()->EnvGet("TZDIR");
    return internal::FindZoneinfoNameByContent(
            kLocaltimePath, tzdir.empty() ? fs::path(kDefaultZoneinfoDir) : fs::path(tzdir));
}

#endif  // _WIN32

}  // namespace

bool IsZoneinfoName(std::string_view name) {
    const std::vector<std::string_view> parts = absl::StrSplit(name, '/');
    return (parts.size() == 2 || parts.size() == 3) &&
           std::none_of(parts.begin(), parts.end(),
                        [](std::string_view part) { return part.empty(); });
}

std::optional<std::string> GetHostTimezone() {
    return DetectHostTimezone();
}

std::optional<std::string> GetGuestTimezone(const char* requested,
                                            std::optional<std::string> host_timezone) {
    if (requested != nullptr && IsZoneinfoName(requested)) {
        return std::string(requested);
    }
    return host_timezone;
}

namespace internal {

std::span<const WindowsZone> WindowsZones() {
    return kWindowsZones;
}

std::optional<std::string_view> Win32TimezoneToZoneinfo(std::string_view key_name,
                                                        std::string_view standard_name) {
    if (auto zoneinfo_name = LookUpWindowsZone(key_name)) {
        return zoneinfo_name;
    }
    return LookUpWindowsZone(standard_name);
}

std::optional<std::string> ZoneinfoNameFromLink(std::string_view link_target) {
    constexpr std::string_view kZoneinfoDir = "zoneinfo/";
    // Find the last "zoneinfo/" path component.
    size_t pos = link_target.rfind(kZoneinfoDir);
    while (pos != std::string_view::npos && pos > 0 && link_target[pos - 1] != '/') {
        pos = link_target.rfind(kZoneinfoDir, pos - 1);
    }
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view zoneinfo_name = link_target.substr(pos + kZoneinfoDir.size());
    if (const size_t slash = zoneinfo_name.find('/');
        slash != std::string_view::npos && IsZoneinfoCopyDir(zoneinfo_name.substr(0, slash))) {
        zoneinfo_name.remove_prefix(slash + 1);
    }
    if (!IsZoneinfoName(zoneinfo_name)) {
        return std::nullopt;
    }
    return std::string(zoneinfo_name);
}

std::optional<std::string> FindZoneinfoNameByContent(const fs::path& localtime,
                                                     const fs::path& zoneinfo_dir) {
    const absl::StatusOr<std::string> localtime_content =
            android::base::file::read_whole_file(localtime, /*binary=*/true);
    if (!localtime_content.ok()) {
        return std::nullopt;
    }

    std::vector<fs::path> zone_files;
    CollectZoneFiles(zoneinfo_dir, fs::path(), /*depth=*/0, &zone_files);
    // Aliases can have the same content, so pick the first name in a stable
    // order.
    std::sort(zone_files.begin(), zone_files.end());

    for (const fs::path& zone_file : zone_files) {
        const fs::path path = zoneinfo_dir / zone_file;
        // Only read the files that have the right size.
        const absl::StatusOr<android::base::StorageCapacity> size =
                android::base::file::file_size(path);
        if (!size.ok() || size->Bytes() != localtime_content->size()) {
            continue;
        }
        const absl::StatusOr<std::string> content =
                android::base::file::read_whole_file(path, /*binary=*/true);
        if (content.ok() && *content == *localtime_content) {
            return zone_file.generic_string();
        }
    }
    return std::nullopt;
}

}  // namespace internal
}  // namespace goldfish::timezone
