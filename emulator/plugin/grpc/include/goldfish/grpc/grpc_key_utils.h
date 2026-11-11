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

#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace goldfish::grpc {

/**
 * @brief Parses custom JWT public keys and writes pre-validated JWK files to jwk_dir.
 *
 * Each key entry in @p custom_keys can be a file path or raw JSON string formatted as a
 * JSON Web Key (JWK) or JWK Set per the specification:
 * https://openid.net/specs/draft-jones-json-web-key-03.html
 *
 * Single JWK key objects are automatically wrapped into a JWK Set structure (containing a
 * "keys" array). Validates each key set with Tink before writing custom_key_<N>.jwk into @p
 * jwk_dir.
 *
 * @param custom_keys List of key input strings (file paths or raw JWK/JWK Set JSON) provided via
 * QOM properties.
 * @param jwk_dir Existing directory path where JWK files are written.
 *
 * @return absl::OkStatus() on success.
 * @return absl::FailedPreconditionError if @p jwk_dir does not exist.
 * @return absl::InvalidArgumentError if any key format or pre-validation fails.
 */
absl::Status ProcessCustomJwtKeys(const std::vector<std::string>& custom_keys,
                                  const std::filesystem::path& jwk_dir);

/**
 * @brief Configures @p jwk_dir with any @p custom_keys and returns the active JWK output path.
 *
 * Writes any custom JWT public keys into `custom_key_<N>.jwk` inside @p jwk_dir and returns
 * `@p jwk_dir / "active.jwk"`, which is passed as `jwks_loaded_path` to `JwtTokenAuth` and
 * advertised as `grpc.jwk_active` in the emulator discovery file.
 *
 * @param jwk_dir Existing directory path where JWK files are watched and written.
 * @param custom_keys Optional list of custom JWT public keys (file paths or raw JWK/JWK Set JSON).
 *
 * @return Path to `@p jwk_dir / "active.jwk"` on success.
 * @return absl::FailedPreconditionError if @p jwk_dir does not exist.
 * @return absl::InvalidArgumentError if any custom key format or pre-validation fails.
 */
absl::StatusOr<std::filesystem::path> ConfigureJwtDirectory(
        const std::filesystem::path& jwk_dir, const std::vector<std::string>& custom_keys);

}  // namespace goldfish::grpc
