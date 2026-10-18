// Copyright (C) 2023 The Android Open Source Project
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
#include "android/emulation/control/jwk_key_loader.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "goldfish/file/file.h"
#include "tink/jwt/jwk_set_converter.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) printf("JwkKeyLoader: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::emulation::control {

void JwkKeyLoader::Clear() {
    const std::lock_guard guard(keylock_);
    public_keys_.clear();
}

bool JwkKeyLoader::Empty() const {
    const std::lock_guard guard(keylock_);
    return public_keys_.empty();
}

int JwkKeyLoader::Size() const {
    const std::lock_guard guard(keylock_);
    return static_cast<int>(public_keys_.size());
}

namespace {
// Reads a file into a string.
absl::StatusOr<std::string> ReadFile(const JwkKeyLoader::Path& fname) {
    const std::string path_str = fname.string();
    if (!base::file::is_file(fname)) {
        return absl::NotFoundError(absl::StrCat(
                "Failed to read JWK file '", path_str,
                "': path does not exist or is not a regular file. Verify the file path and ensure "
                "the JWK file exists."));
    }

    if (!base::file::can_read(fname)) {
        return absl::PermissionDeniedError(
                absl::StrCat("Failed to read JWK file '", path_str,
                             "': permission denied. Grant read access to the file and try again."));
    }

    // Open stream at the end so we can learn the size.
    errno = 0;
    std::ifstream fstream(fname, std::ios::binary | std::ios::ate);
    if (fstream.fail()) {
        const int err = errno;
        return absl::UnavailableError(absl::StrFormat(
                "Failed to open JWK file '%s': %s. Retry if the file is still being written.",
                path_str, err != 0 ? std::strerror(err) : "stream open failed"));
    }

    const std::streampos file_size = fstream.tellg();
    if (file_size == 0) {
        return absl::UnavailableError(absl::StrFormat(
                "Failed to read JWK file '%s': file is empty (0 bytes). Retry once the JWK set "
                "has been written.",
                path_str));
    }

    // Usually a jkw file is around 300 bytes... so...
    constexpr int kMaxJwkSize = 8 * 1024;
    if (file_size > kMaxJwkSize) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to read JWK file '%s': file size (%d bytes) exceeds maximum allowed size "
                "of %d bytes. Provide a JWK file of at most %d bytes.",
                path_str, static_cast<int>(file_size), kMaxJwkSize, kMaxJwkSize));
    }

    // Allocate the string with the required size and read it.
    std::string contents(file_size, ' ');
    errno = 0;
    fstream.seekg(0, std::ios::beg);
    fstream.read(contents.data(), file_size);

    if (fstream.bad() || fstream.fail()) {
        const int err = errno;
        return absl::InternalError(absl::StrFormat(
                "Failed to read %d bytes from JWK file '%s': %s. Check file permissions and disk "
                "health.",
                static_cast<int>(file_size), path_str,
                err != 0 ? std::strerror(err) : "short or failed read"));
    }

    return contents;
}
}  // namespace

absl::Status JwkKeyLoader::AddWithRetryForEmpty(const Path& to_add, int retries,
                                                std::chrono::milliseconds wait_for) {
    absl::Status status;
    do {
        status = Add(to_add);
        if (status.code() == absl::StatusCode::kUnavailable) {
            LOG(INFO) << "Token not yet available, waiting " << wait_for.count() << " ms.";
            std::this_thread::sleep_for(wait_for);
        }
        retries--;
    } while (status.code() == absl::StatusCode::kUnavailable && retries > 0);

    return status;
}

absl::Status JwkKeyLoader::Add(const Path& to_add) {
    auto json_string = ReadFile(to_add);
    if (!json_string.ok()) return json_string.status();

    if (!json::accept(*json_string)) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to load JWK file '%s': file content (%d bytes) is not valid JSON. "
                "Provide a valid RFC 7517 JSON Web Key Set.",
                to_add.string(), static_cast<int>(json_string->size())));
    }

    return Add(to_add, *json_string);
}

absl::Status JwkKeyLoader::Add(const Path& to_add, const std::string& json_string) {
    auto handle = crypto::tink::JwkSetToPublicKeysetHandle(json_string);
    if (!handle.ok()) {
        LOG(INFO) << "Rejected invalid JWK set in '" << to_add.string()
                  << "': " << handle.status().message();
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to parse JWK set from '%s': %s. Provide a valid RFC 7517 public JWK set.",
                to_add.string(), handle.status().message()));
    }

    auto json_snippet = crypto::tink::JwkSetFromPublicKeysetHandle(**handle);
    if (!json_snippet.ok()) {
        return absl::InternalError(absl::StrFormat(
                "Failed to serialize public keyset handle to JWK JSON for '%s': %s.",
                to_add.string(), json_snippet.status().message()));
    }

    json object = json::parse(*json_snippet);
    DCHECK(!object.is_discarded());

    // We should have a "keys" array (see:
    // https://datatracker.ietf.org/doc/html/rfc7517)
    DCHECK(object.find("keys") != object.end());

    // Note: At least one key will be present.
    auto keys = object["keys"];
    auto kid = keys.front().find("kid");
    if (kid == keys.front().end()) {
        LOG(WARNING) << "No KeyID found in JWK " << to_add;
    } else {
        DD("KeyID %s, path: %s", kid->get<std::string>().c_str(), to_add.c_str());
    }

    {
        const std::lock_guard guard(keylock_);
        public_keys_[to_add] = keys;
    }
    return absl::OkStatus();
}

absl::Status JwkKeyLoader::Remove(const Path& to_remove) {
    const std::lock_guard guard(keylock_);
    if (!public_keys_.contains(to_remove)) {
        return absl::NotFoundError(absl::StrFormat(
                "Failed to remove JWK file '%s': no loaded public keyset is registered for this "
                "path.",
                to_remove.string()));
    }

    public_keys_.erase(to_remove);
    return absl::OkStatus();
}

JwkKeyLoader::json JwkKeyLoader::ActiveKeysetAsJson() const {
    // Now let's reconstruct our key set.
    std::vector<json> keys;
    {
        const std::lock_guard guard(keylock_);
        for (const auto& [fname, keyset] : public_keys_) {
            // contents is a json JWKS, so let's merge them together.
            for (const auto& key : keyset) {
                keys.emplace_back(key);
            }
        }
    }

    json combined_jwk = {{"keys", keys}};
    return combined_jwk;
}

std::string JwkKeyLoader::ActiveKeysetAsString() const {
    constexpr int kIndentation = 2;
    return ActiveKeysetAsJson().dump(kIndentation);
}

absl::StatusOr<JwkKeyLoader::Keyset> JwkKeyLoader::ActiveKeySet() const {
    if (Empty()) {
        return absl::FailedPreconditionError(
                "Failed to build active JWK keyset: no JWK files are loaded (keys list is empty). "
                "Load at least one valid JWK file before requesting the active keyset.");
    }
    auto json_keys = ActiveKeysetAsString();
    auto key_handle = crypto::tink::JwkSetToPublicKeysetHandle(json_keys);
    if (!key_handle.ok()) {
        return absl::InternalError(
                absl::StrFormat("Failed to build active JWK keyset from %d loaded JWK file(s): %s.",
                                Size(), key_handle.status().message()));
    }

    return {std::move(key_handle.value())};
}

}  // namespace android::emulation::control
