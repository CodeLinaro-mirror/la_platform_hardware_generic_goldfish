// Copyright 2026 The Android Open Source Project
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
#include <optional>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace goldfish::fsimage {

namespace fs = std::filesystem;

namespace internal {

absl::StatusOr<std::optional<uint64_t>> ReadQcow2VirtualSize(const fs::path& qcow2_path);

absl::Status CreateQcow2Overlay(const fs::path& qcow2_image, std::string_view backing_path,
                                std::string_view backing_format, uint64_t raw_size);

}  // namespace internal

}  // namespace goldfish::fsimage
