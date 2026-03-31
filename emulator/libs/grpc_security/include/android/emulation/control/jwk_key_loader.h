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

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "nlohmann/json.hpp"
#include "tink/keyset_handle.h"

namespace android::emulation::control {

// A class for loading JSON Web Key (JWK) sets from files and managing the
// active set of keys.
class JwkKeyLoader {
  public:
    using Path = std::filesystem::path;
    using json = nlohmann::json;
    using Keyset = std::unique_ptr<crypto::tink::KeysetHandle>;

    JwkKeyLoader() = default;
    ~JwkKeyLoader() = default;

    void Clear();
    bool Empty() const;
    int Size() const;

    /**
     * Loads a JWK set from a file and adds it to the collection.
     * This will attempt to re-access empty files, which can happen
     * in certain windows configuration (gWindows).
     *
     * Note: This is a workaround for b/274984168 where we see anti
     * virus software temporarily blocking file access.
     *
     * @param toAdd The path to the file containing the JWK set.
     * @param retries The maximum number of attempts we make
     * @param wait_for The delay between each retry attempt.
     * @return An absl::Status indicating success or failure of the operation.
     */
    absl::Status AddWithRetryForEmpty(const Path& to_add, int retries,
                                      std::chrono::milliseconds wait_for);

    /**
     * Loads a JWK set from a file and adds it to the collection.
     *
     * @param toAdd The path to the file containing the JWK set.
     * @return An absl::Status indicating success or failure of the operation.
     */
    absl::Status Add(const Path& to_add);

    /**
     * Loads a JWK set from a JSON string and adds it to the collection.
     *
     * @param toAdd The path to associate with the JWK set.
     * @param jsonString The JSON string containing the JWK set.
     * @return An absl::Status indicating success or failure of the operation.
     */
    absl::Status Add(const Path& to_add, const std::string& json_string);

    /**
     * Removes a JWK set from the collection.
     *
     * @param toRemove The path associated with the JWK set to remove.
     * @return An absl::Status indicating success or failure of the operation.
     */
    absl::Status Remove(const Path& to_remove);

    /**
     * Returns the active JWK keyset as a Tink KeysetHandle.
     *
     * @return An absl::StatusOr containing the active keyset or an error
     * status.
     */
    absl::StatusOr<Keyset> ActiveKeySet() const;

    /**
     * Returns the active JWK keyset as a JSON object.
     *
     * @return The active keyset as a JSON object.
     */
    json ActiveKeysetAsJson() const;

    /**
     * Returns the active JWK keyset as a JSON string.
     *
     * @return The active keyset as a JSON string.
     */
    std::string ActiveKeysetAsString() const;

  private:
    std::unordered_map<Path, json> public_keys_;
    mutable std::mutex keylock_;
};

}  // namespace android::emulation::control
