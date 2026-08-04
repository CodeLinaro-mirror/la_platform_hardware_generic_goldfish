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

#include "goldfish/grpc/grpc_key_utils.h"

#include <filesystem>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"
#include "nlohmann/json.hpp"
#include "tink/jwt/jwk_set_converter.h"

namespace fs = std::filesystem;
namespace file = ::android::base::file;
using json = nlohmann::json;

namespace goldfish::grpc {

namespace {

/**
 * @brief Converts a user-provided key input string (file path or JWK JSON string)
 *        into a validated JWK Set JSON string.
 */
absl::StatusOr<std::string> ConvertToJwkSetJson(std::string input) {
    if (input.empty()) {
        return absl::InvalidArgumentError("Custom JWT public key input is empty");
    }

    if (input[0] != '{' && file::is_file(input)) {
        auto read_res = file::read_whole_file(input, /*binary=*/false);
        if (!read_res.ok()) {
            return absl::InvalidArgumentError("Failed to read custom JWT key file " + input + ": " +
                                              read_res.status().ToString());
        }
        input = std::move(*read_res);
    }

    json j = json::parse(input, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        return absl::InvalidArgumentError("Invalid custom JWT public key format: " + input);
    }

    if (!j.contains("keys")) {
        j = json{{"keys", json::array({j})}};
    }
    return j.dump();
}

}  // namespace

absl::Status ProcessCustomJwtKeys(const std::vector<std::string>& custom_keys,
                                  const fs::path& jwk_dir) {
    if (custom_keys.empty()) {
        return absl::OkStatus();
    }

    if (!file::is_dir(jwk_dir)) {
        return absl::FailedPreconditionError("JWK directory does not exist: " + jwk_dir.string());
    }

    for (size_t i = 0; i < custom_keys.size(); ++i) {
        ASSIGN_OR_RETURN(std::string jwk_json_str, ConvertToJwkSetJson(custom_keys[i]));

        auto handle = crypto::tink::JwkSetToPublicKeysetHandle(jwk_json_str);
        if (!handle.ok()) {
            return absl::InvalidArgumentError("Custom JWT public key pre-validation failed: " +
                                              std::string(handle.status().message()));
        }

        fs::path out_path = jwk_dir / absl::StrFormat("custom_key_%zu.jwk", i);
        if (file::exists(out_path)) {
            file::rm(out_path).IgnoreError();
        }
        auto write_status = file::CreatePrivateFileExclusive(out_path, jwk_json_str);
        if (!write_status.ok()) {
            return absl::InternalError("Failed to write custom JWK file to " + out_path.string() +
                                       ": " + write_status.ToString());
        }
        LOG(INFO) << "Registered custom JWT public key at: " << out_path;
    }
    return absl::OkStatus();
}

}  // namespace goldfish::grpc
