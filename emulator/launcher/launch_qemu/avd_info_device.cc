// Copyright 2025 The Android Open Source Project
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

#include "avd_info_device.h"

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "goldfish/metrics/configure_metrics_writer.h"

namespace android::goldfish {

namespace {
void AppendMetricsConfigString(std::string* s, const MetricsConfig& config) {
    absl::StrAppend(s, ",metrics_session=", config.session_id);
    absl::StrAppend(s, ",metrics_writer=", static_cast<uint32_t>(config.writer_config.type));
    switch (config.writer_config.type) {
        using enum ::goldfish::metrics::MetricsWriterType;
    case kFile:
        absl::StrAppend(s, ",metrics_file_path=", config.writer_config.file_path.string());
        break;
    case kStudio:
        absl::StrAppend(s, ",metrics_spool_dir=", config.writer_config.studio_spool_dir.string());
        break;
    case kPlaystore:
        absl::StrAppend(s, ",metrics_playstore_url=", config.writer_config.playstore_url);
        absl::StrAppend(s, ",metrics_user_id=", config.writer_config.user_id);
        break;
    case kNone:
    case kConsole:
        break;
    }
}
}  // namespace

absl::Status AvdInfoDevice::initialize(const EmulatorConfig& emulator) {
    std::vector<std::pair<std::string, std::string>> params{
        {"serial_number", absl::StrCat(emulator.serial_number())},
        {"adb_port", absl::StrCat(emulator.adb_port())},
        {"avd_name", emulator.avd().DisplayName()},
        {"avd_id", emulator.avd().Id()},
        {"avd_abi", emulator.avd().Abi()},
        {"avd_api", absl::StrCat(emulator.avd().ApiLevel())},
        {"avd_type", absl::StrCat(static_cast<int32_t>(emulator.avd().GetDeviceType()))},
        {"avd_dir", emulator.avd().GetContentPath().string()},
        {"build_sdk", emulator.avd().BuildSdk()},
        {"build_id", emulator.avd().BuildId()},
        {"build_flavour", emulator.avd().BuildFlavour()},
    };

    if (char* perf_stat = emulator.opts().perf_stat) {
        fs::path perf_stat_path(perf_stat);
        params.emplace_back("dump_perf_stat_path", perf_stat_path.string());
    }

    if (char* snapshot = emulator.opts().snapshot) {
        params.emplace_back("snapshot_name", snapshot);
    }

    mAvdParams = absl::StrJoin(params, ",", [](std::string* s, const auto& pair) {
        absl::StrAppend(s, pair.first, "=", pair.second);
    });

    if (char* quit_after_boot = emulator.opts().quit_after_boot) {
        if (int timeout; absl::SimpleAtoi(quit_after_boot, &timeout)) {
            absl::StrAppend(&mAvdParams, ",quit_after_boot_timeout=", timeout);
        } else {
            return absl::InvalidArgumentError(absl::StrCat(
                    "Failed to parse -quit-after-boot parameter as int: ", quit_after_boot));
        }
    }

    AppendMetricsConfigString(&mAvdParams, emulator.metrics_config());


    return absl::OkStatus();
}

std::vector<std::string> AvdInfoDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-device", absl::StrCat("avdstart,", mAvdParams)};
}

}  // namespace android::goldfish
