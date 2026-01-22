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

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/base/file/file.h"
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
    if (!base::file::is_file(fname)) {
        return absl::NotFoundError(absl::StrCat("The path: ", fname.string(), " does not exist."));
    }

    if (!base::file::can_read(fname)) {
        return absl::NotFoundError(absl::StrCat("The path: ", fname.string(), " is not readable."));
    }

    // Open stream at the end so we can learn the size.
    std::ifstream fstream(fname, std::ios::binary | std::ios::ate);
    const std::streampos file_size = fstream.tellg();

    if (file_size == 0) {
        auto message = absl::StrFormat("%s is empty", fname);
        return absl::UnavailableError(message);
    }

    if (fstream.fail()) {
        auto message = absl::StrFormat("%s failed to open.", fname);
        return absl::UnavailableError(message);
    }

    // Usually a jkw file is around 300 bytes... so...
    constexpr int kMaxJwkSize = 8 * 1024;
    if (file_size > kMaxJwkSize) {
        auto message =
                absl::StrFormat("Refusing to read %s of size %d, which is over our max of %d.",
                                fname, static_cast<int>(file_size), kMaxJwkSize);
        return absl::PermissionDeniedError(message);
    }

    // Allocate the string with the required size and read it.
    std::string contents(file_size, ' ');
    fstream.seekg(0, std::ios::beg);
    fstream.read(contents.data(), file_size);

    if (fstream.bad()) {
        auto message =
                absl::StrFormat("Failure reading %s due to: %s", fname, std::strerror(errno));
        return absl::InternalError(message);
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
        return absl::InternalError(
                absl::StrFormat("%s contains: %s, an invalid json object.", to_add, *json_string));
    }

    return Add(to_add, *json_string);
}

absl::Status JwkKeyLoader::Add(const Path& to_add, const std::string& json_string) {
    auto handle = crypto::tink::JwkSetToPublicKeysetHandle(json_string);
    if (!handle.ok()) {
        LOG(INFO) << to_add << " contains " << json_string << ", which is invalid.";
        return absl::InternalError(absl::StrFormat("%s does not contain a valid jwk", to_add));
    }

    auto json_snippet = crypto::tink::JwkSetFromPublicKeysetHandle(**handle);
    if (!json_snippet.ok()) {
        return absl::InternalError(
                absl::StrFormat("Unable to convert key handle to json for file: %s", to_add));
    }

    json object = json::parse(*json_snippet);
    assert(!object.is_discarded());

    // We should have a "keys" array (see:
    // https://datatracker.ietf.org/doc/html/rfc7517)
    assert(object.find("keys") != object.end());

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
        return absl::NotFoundError(
                absl::StrFormat("Public key with path %s, does not exist", to_remove));
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
    auto json_keys = ActiveKeysetAsString();
    auto key_handle = crypto::tink::JwkSetToPublicKeysetHandle(json_keys);
    if (!key_handle.ok()) {
        return absl::InternalError(key_handle.status().message());
    }

    return {std::move(key_handle.value())};
}

}  // namespace android::emulation::control
