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

#include "goldfish/metrics/studio_config.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/util/json_util.h"

#include "analytics_settings.pb.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"
#include "goldfish/metrics/uuid.h"

namespace goldfish::metrics::studio {

namespace {

fs::path GetAnalyticsSettingsPath(const fs::path& user_directory) {
    return user_directory / "analytics.settings";
}

bool ParseAnalyticsSettingsJson(const fs::path& user_directory,
                                android::analytics::AnalyticsSettings* settings) {
    fs::path settings_path = GetAnalyticsSettingsPath(user_directory);
    std::ifstream is(settings_path);
    if (!is.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << is.rdbuf();
    std::string jsonStr = buffer.str();

    // Java's Gson serializers write BigIntegers out as unquoted raw numbers, which
    // are too large for standard 64-bit int parsers. We must use a quick regex to stringify
    // them before Protobuf strictly parses the payload.
    std::regex saltRegex(R"(\"saltValue\"\s*:\s*(-?\d+))");
    jsonStr = std::regex_replace(jsonStr, saltRegex, R"("saltValue":"$1")");

    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;
    auto status = google::protobuf::util::JsonStringToMessage(jsonStr, settings, options);
    return status.ok();
}

int32_t GetCurrentSaltSkew(std::optional<absl::Time> now = std::nullopt) {
    absl::Time target_time = now.value_or(absl::Now());

    // Let absl handle the safe duration math
    int64_t days_since_epoch = absl::ToInt64Hours(target_time - absl::UnixEpoch()) / 24 + 3;
    return static_cast<int32_t>(days_since_epoch / 28);
}

bool IsSaltRotationNeeded(int32_t dataSkew, int32_t currentSkew) {
    int32_t dataSkew532 = (dataSkew - 11) / 19;
    int32_t currentSkew532 = (currentSkew - 11) / 19;
    return dataSkew532 != currentSkew532;
}

bool SaveAnalyticsSettings(const fs::path& settingsPath,
                           const android::analytics::AnalyticsSettings& settings) {
    google::protobuf::util::JsonPrintOptions printOptions;
    printOptions.always_print_fields_with_no_presence = false;
    printOptions.preserve_proto_field_names = false;

    std::string jsonStr;
    auto status = google::protobuf::util::MessageToJsonString(settings, &jsonStr, printOptions);
    if (!status.ok()) {
        return false;
    }

    std::regex stripQuotesRegex(R"(\"saltValue\"\s*:\s*\"(-?\d+)\")");
    jsonStr = std::regex_replace(jsonStr, stripQuotesRegex, R"("saltValue":$1)");

    if (!jsonStr.empty() && jsonStr.back() != '\n') {
        jsonStr.push_back('\n');
    }

    fs::path tempPath = settingsPath;
    tempPath += ".tmp." + Uuid::Generate().ToString();

    if (!android::base::file::CreatePrivateFileExclusive(tempPath, jsonStr).ok()) {
        return false;
    }

    return android::base::file::mv_file(tempPath, settingsPath).ok();
}

}  // namespace

namespace testing {
std::string GenerateRandomSalt(const std::vector<uint8_t>& customBytes) {
    // Android Studio's BigInteger expects exactly 24 bytes of entropy.
    constexpr size_t kSaltByteSize = 24;
    std::vector<uint8_t> bytes(kSaltByteSize);
    if (!customBytes.empty() && customBytes.size() == kSaltByteSize) {
        bytes = customBytes;
    } else {
        std::random_device rd;
        for (size_t i = 0; i < kSaltByteSize; ++i) {
            bytes[i] = static_cast<uint8_t>(rd());
        }
    }

    bool negative = (bytes[0] & 0x80) != 0;
    std::vector<uint8_t> magnitude = bytes;

    // Java's BigInteger Two's Complement equivalent:
    // If the highest bit is set, it's negative. We calculate the absolute magnitude
    // by inverting all bits and adding one, then we string-format it and prepend a '-'.
    if (negative) {
        uint16_t carry = 1;
        for (int i = kSaltByteSize - 1; i >= 0; --i) {
            uint16_t val = static_cast<uint8_t>(~magnitude[i]) + carry;
            magnitude[i] = static_cast<uint8_t>(val & 0xFF);
            carry = val >> 8;
        }
    }

    std::string digits;
    auto isZero = [](const std::vector<uint8_t>& mag) {
        for (uint8_t b : mag) {
            if (b != 0) return false;
        }
        return true;
    };

    while (!isZero(magnitude)) {
        uint32_t remainder = 0;
        for (size_t i = 0; i < magnitude.size(); ++i) {
            uint32_t current = (remainder << 8) | magnitude[i];
            magnitude[i] = static_cast<uint8_t>(current / 10);
            remainder = current % 10;
        }
        digits.push_back('0' + remainder);
    }

    if (digits.empty()) {
        digits = "0";
    } else {
        if (negative) {
            digits.push_back('-');
        }
        std::reverse(digits.begin(), digits.end());
    }
    return digits;
}
}  // namespace testing

void EnsureAnalyticsSettings(const fs::path& user_directory, std::optional<absl::Time> now) {
    fs::path settingsPath = GetAnalyticsSettingsPath(user_directory);
    android::analytics::AnalyticsSettings settings;
    bool exists = ParseAnalyticsSettingsJson(user_directory, &settings);
    int32_t currentSkew = GetCurrentSaltSkew(now);

    if (!exists) {
        settings.set_user_id(Uuid::Generate().ToString());
        settings.set_opted_in(false);
        settings.set_salt_value(testing::GenerateRandomSalt());
        settings.set_salt_skew(currentSkew);
        SaveAnalyticsSettings(settingsPath, settings);
    } else {
        bool needsUpdate = false;
        if (settings.user_id().empty()) {
            settings.set_user_id(Uuid::Generate().ToString());
            needsUpdate = true;
        }
        if (settings.salt_value().empty() || settings.salt_skew() <= 0 ||
            IsSaltRotationNeeded(settings.salt_skew(), currentSkew)) {
            settings.set_salt_value(testing::GenerateRandomSalt());
            settings.set_salt_skew(currentSkew);
            needsUpdate = true;
        }
        if (needsUpdate) {
            SaveAnalyticsSettings(settingsPath, settings);
        }
    }
}

std::string GetAnonymizationSalt(const fs::path& user_directory) {
    android::analytics::AnalyticsSettings settings;
    if (ParseAnalyticsSettingsJson(user_directory, &settings) && !settings.salt_value().empty()) {
        return settings.salt_value();
    }
    return "UNKNOWN_SALT";
}

OptInState GetUserMetricsOptIn(const fs::path& user_directory) {
    android::analytics::AnalyticsSettings settings;
    if (!ParseAnalyticsSettingsJson(user_directory, &settings)) {
        return OptInState::kUnknown;
    }

    // Default to opted out since proto3 emits false booleans as un-serialized absent keys.
    return settings.opted_in() ? OptInState::kOptedIn : OptInState::kOptedOut;
}

std::string GetMetricsUserId(const fs::path& user_directory) {
    android::analytics::AnalyticsSettings settings;
    if (!ParseAnalyticsSettingsJson(user_directory, &settings)) {
        return {};
    }
    return settings.user_id();
}

fs::path GetSpoolDirectory(const fs::path& user_directory) {
    return user_directory / "metrics" / "spool";
}

}  // namespace goldfish::metrics::studio
