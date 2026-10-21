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
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"

namespace goldfish::timezone {
namespace {

namespace fs = std::filesystem;

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::Optional;

// Names returned by strftime("%Z") on localized Windows installations. They
// are localized display names, not timezone key names.
// "Tokyo Standard Time" in Japanese (UTF-8).
constexpr char kJapaneseTokyoStandardName[] =
        "\xe6\x9d\xb1\xe4\xba\xac (\xe6\xa8\x99\xe6\xba\x96\xe6\x99\x82)";
// "W. Europe Standard Time" in Norwegian.
constexpr char kNorwegianWEuropeStandardName[] = "Vest-Europa (normaltid)";

TEST(HostTimezoneTest, IsZoneinfoNameAcceptsAreaLocation) {
    EXPECT_THAT(IsZoneinfoName("Europe/Paris"), IsTrue());
    EXPECT_THAT(IsZoneinfoName("America/Argentina/Buenos_Aires"), IsTrue());
    EXPECT_THAT(IsZoneinfoName("Etc/GMT-13"), IsTrue());
}

TEST(HostTimezoneTest, IsZoneinfoNameRejectsOtherNames) {
    EXPECT_THAT(IsZoneinfoName(""), IsFalse());
    EXPECT_THAT(IsZoneinfoName("JST"), IsFalse());
    EXPECT_THAT(IsZoneinfoName("Europe/"), IsFalse());
    EXPECT_THAT(IsZoneinfoName("/Paris"), IsFalse());
    EXPECT_THAT(IsZoneinfoName("Europe//Paris"), IsFalse());
    EXPECT_THAT(IsZoneinfoName("A/B/C/D"), IsFalse());
}

TEST(HostTimezoneTest, GetGuestTimezonePrefersRequestedTimezone) {
    EXPECT_THAT(GetGuestTimezone("Europe/Paris", "Asia/Tokyo"), Optional(Eq("Europe/Paris")));
}

TEST(HostTimezoneTest, GetGuestTimezoneIgnoresInvalidRequestedTimezone) {
    EXPECT_THAT(GetGuestTimezone("JST", "Asia/Tokyo"), Optional(Eq("Asia/Tokyo")));
    EXPECT_THAT(GetGuestTimezone("JST", std::nullopt), Eq(std::nullopt));
}

TEST(HostTimezoneTest, GetGuestTimezoneDefaultsToHostTimezone) {
    EXPECT_THAT(GetGuestTimezone(nullptr, "Asia/Tokyo"), Optional(Eq("Asia/Tokyo")));
    EXPECT_THAT(GetGuestTimezone(nullptr, std::nullopt), Eq(std::nullopt));
}

#ifndef _WIN32
TEST(HostTimezoneTest, GetHostTimezoneUsesTzEnvironmentVariable) {
    android::base::TestSystem test_system("/tmp");

    test_system.EnvSet("TZ", ":Asia/Tokyo");
    EXPECT_THAT(GetHostTimezone(), Optional(Eq("Asia/Tokyo")));

    test_system.EnvSet("TZ", "America/Argentina/Buenos_Aires");
    EXPECT_THAT(GetHostTimezone(), Optional(Eq("America/Argentina/Buenos_Aires")));
}
#endif

TEST(HostTimezoneTest, GetHostTimezoneReturnsZoneinfoNameWhenKnown) {
    const std::optional<std::string> host_timezone = GetHostTimezone();
    if (host_timezone) {
        EXPECT_THAT(IsZoneinfoName(*host_timezone), IsTrue()) << *host_timezone;
    }
}

TEST(HostTimezoneTest, WindowsZonesAreSortedUniqueAndValid) {
    const auto zones = internal::WindowsZones();
    ASSERT_THAT(zones, Not(IsEmpty()));
    EXPECT_THAT(std::adjacent_find(zones.begin(), zones.end(),
                                   [](const auto& a, const auto& b) {
                                       return !(a.key_name < b.key_name);
                                   }) == zones.end(),
                IsTrue());
    for (const auto& zone : zones) {
        EXPECT_THAT(IsZoneinfoName(zone.zoneinfo_name), IsTrue()) << zone.key_name;
    }
}

TEST(HostTimezoneTest, Win32TimezoneToZoneinfoMapsKeyNames) {
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Tokyo Standard Time", ""),
                Optional(Eq("Asia/Tokyo")));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("W. Europe Standard Time", ""),
                Optional(Eq("Europe/Berlin")));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Pacific Standard Time", ""),
                Optional(Eq("America/Los_Angeles")));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("UTC", ""), Optional(Eq("Etc/UTC")));
    // Asia/Almaty moved to UTC+5 in 2024 and America/Chihuahua to UTC-6 in
    // 2022, so these UTC+6 and UTC-7 timezones map to other zones.
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Central Asia Standard Time", ""),
                Optional(Eq("Asia/Bishkek")));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Mountain Standard Time (Mexico)", ""),
                Optional(Eq("America/Mazatlan")));
}

TEST(HostTimezoneTest, Win32TimezoneToZoneinfoUsesKeyNameOnLocalizedWindows) {
    // Japanese Windows.
    EXPECT_THAT(
            internal::Win32TimezoneToZoneinfo("Tokyo Standard Time", kJapaneseTokyoStandardName),
            Optional(Eq("Asia/Tokyo")));
    // Norwegian Windows.
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("W. Europe Standard Time",
                                                  kNorwegianWEuropeStandardName),
                Optional(Eq("Europe/Berlin")));
}

TEST(HostTimezoneTest, Win32TimezoneToZoneinfoPrefersKeyName) {
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Tokyo Standard Time", "Pacific Standard Time"),
                Optional(Eq("Asia/Tokyo")));
    // Even on English Windows, the standard name of the "UTC" key differs from
    // the key name.
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("UTC", "Coordinated Universal Time"),
                Optional(Eq("Etc/UTC")));
}

TEST(HostTimezoneTest, Win32TimezoneToZoneinfoFallsBackToStandardName) {
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("", "Tokyo Standard Time"),
                Optional(Eq("Asia/Tokyo")));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("Unmapped Standard Time", "Tokyo Standard Time"),
                Optional(Eq("Asia/Tokyo")));
}

TEST(HostTimezoneTest, Win32TimezoneToZoneinfoReturnsNulloptForUnknownNames) {
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("", ""), Eq(std::nullopt));
    // Localized names cannot be mapped on their own.
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("", kJapaneseTokyoStandardName),
                Eq(std::nullopt));
    EXPECT_THAT(internal::Win32TimezoneToZoneinfo("", kNorwegianWEuropeStandardName),
                Eq(std::nullopt));
}

TEST(HostTimezoneTest, ZoneinfoNameFromLinkFindsZoneName) {
    // Linux, with relative and absolute links.
    EXPECT_THAT(internal::ZoneinfoNameFromLink("../usr/share/zoneinfo/America/Los_Angeles"),
                Optional(Eq("America/Los_Angeles")));
    EXPECT_THAT(
            internal::ZoneinfoNameFromLink("/usr/share/zoneinfo/America/Argentina/Buenos_Aires"),
            Optional(Eq("America/Argentina/Buenos_Aires")));
    // macOS.
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/var/db/timezone/zoneinfo/Asia/Tokyo"),
                Optional(Eq("Asia/Tokyo")));
    // Copies of the database that some distributions install.
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/usr/share/zoneinfo/posix/Europe/Paris"),
                Optional(Eq("Europe/Paris")));
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/usr/share/zoneinfo/right/Europe/Paris"),
                Optional(Eq("Europe/Paris")));
}

TEST(HostTimezoneTest, ZoneinfoNameFromLinkRejectsOtherTargets) {
    EXPECT_THAT(internal::ZoneinfoNameFromLink(""), Eq(std::nullopt));
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/usr/share/zoneinfo/UTC"), Eq(std::nullopt));
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/etc/timezones/Europe/Paris"), Eq(std::nullopt));
    EXPECT_THAT(internal::ZoneinfoNameFromLink("/opt/myzoneinfo/Europe/Paris"), Eq(std::nullopt));
}

class FindZoneinfoNameByContentTest : public ::testing::Test {
  protected:
    void SetUp() override {
        WriteFile("zoneinfo/Europe/Paris", "paris");
        WriteFile("zoneinfo/Asia/Tokyo", "tokyo");
        WriteFile("zoneinfo/America/Argentina/Buenos_Aires", "buenos aires");
        // Not an Area/Location zone.
        WriteFile("zoneinfo/UTC", "utc");
        // Copy of the database that some distributions install.
        WriteFile("zoneinfo/posix/Asia/Kolkata", "kolkata");
    }

    void WriteFile(const fs::path& relative_path, std::string_view content) {
        ASSERT_THAT(temp_dir_.MakeSubDir(relative_path.parent_path()), IsTrue());
        std::ofstream(temp_dir_.MakeSubPath(relative_path), std::ios::binary) << content;
    }

    std::optional<std::string> FindZoneinfoNameFor(std::string_view localtime_content) {
        WriteFile("localtime", localtime_content);
        return internal::FindZoneinfoNameByContent(temp_dir_.MakeSubPath("localtime"),
                                                   temp_dir_.MakeSubPath("zoneinfo"));
    }

    const android::base::TestTempDir temp_dir_{"FindZoneinfoNameByContentTest"};
};

TEST_F(FindZoneinfoNameByContentTest, FindsAreaLocation) {
    EXPECT_THAT(FindZoneinfoNameFor("tokyo"), Optional(Eq("Asia/Tokyo")));
}

TEST_F(FindZoneinfoNameByContentTest, FindsAreaLocationSubLocation) {
    EXPECT_THAT(FindZoneinfoNameFor("buenos aires"),
                Optional(Eq("America/Argentina/Buenos_Aires")));
}

TEST_F(FindZoneinfoNameByContentTest, IgnoresFilesThatAreNotZones) {
    EXPECT_THAT(FindZoneinfoNameFor("utc"), Eq(std::nullopt));
    EXPECT_THAT(FindZoneinfoNameFor("kolkata"), Eq(std::nullopt));
}

TEST_F(FindZoneinfoNameByContentTest, IgnoresFilesBelowSubLocations) {
    WriteFile("zoneinfo/Area/Location/SubLocation/Extra", "too deep");
    EXPECT_THAT(FindZoneinfoNameFor("too deep"), Eq(std::nullopt));
}

TEST_F(FindZoneinfoNameByContentTest, ReturnsNulloptWithoutMatch) {
    EXPECT_THAT(FindZoneinfoNameFor("unknown"), Eq(std::nullopt));
    EXPECT_THAT(internal::FindZoneinfoNameByContent(temp_dir_.MakeSubPath("missing"),
                                                    temp_dir_.MakeSubPath("zoneinfo")),
                Eq(std::nullopt));
}

}  // namespace
}  // namespace goldfish::timezone
