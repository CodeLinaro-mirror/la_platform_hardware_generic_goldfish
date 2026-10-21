// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/metrics/configure_metrics_writer.h"
#include "goldfish/metrics/uuid.h"

namespace goldfish::avd {

struct AvdProperties {
    int32_t serial_number{0};
    int32_t adb_port{0};
    std::string avd_name;
    std::string avd_id;
    std::string avd_abi;
    int32_t avd_api{0};
    std::string avd_api_str;
    android::goldfish::DeviceType avd_type{android::goldfish::DeviceType::kUnknown};
    std::filesystem::path avd_content_path;
    std::string build_sdk;
    std::string build_id;
    std::string build_flavor;
    std::string build_fingerprint;
    std::string snapshot_name;
    bool snapshot_update_time{true};
    // False when the emulator was started with -no-snapshot: no snapshot may
    // be saved or loaded in this session.
    bool snapshots_enabled{true};
    // The snapshot version this emulator writes and accepts, composed by the
    // launcher (see goldfish/snapshot/snapshot_store.h). 0 if not provided.
    int32_t snapshot_version{0};
    int32_t quit_after_boot_timeout_seconds{0};
    ::goldfish::metrics::Uuid metrics_session_id{::goldfish::metrics::Uuid::Zero()};
    ::goldfish::metrics::MetricsWriterConfig metrics_writer_config;
    std::filesystem::path dump_perf_stat_path;
    std::filesystem::path icc_profile;

    android::goldfish::HardwareConfig hw_config;
};

}  // namespace goldfish::avd
