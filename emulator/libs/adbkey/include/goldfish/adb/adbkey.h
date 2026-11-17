// Copyright 2019 The Android Open Source Project
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

#include <openssl/rsa.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "absl/status/statusor.h"

namespace goldfish::adb {

struct AdbKeyResult {
    std::filesystem::path private_key_path;
    // True when `EnsurePrivateAdbKey()` created a new key file via `adb keygen`
    // during this call (rather than reusing an existing key or copying
    // `~/.android/adbkey`). Callers using the shared Android user directory use
    // this to stop any running host `adb` server that started before the key
    // existed so it reloads the new key on restart.
    bool newly_created = false;
};

namespace internal {
// Size of an RSA modulus such as an encrypted block or a signature.
constexpr size_t kAndroidPubkeyModulusSize = 2048 / 8;
// Size of an encoded RSA key.
constexpr size_t kAndroidPubkeyEncodedSize =
        ((3 * sizeof(uint32_t)) + (2 * kAndroidPubkeyModulusSize));

bool AndroidPubkeyDecode(const uint8_t* key_buffer, size_t size, RSA** key);
bool AndroidPubkeyEncode(const RSA* key, uint8_t* key_buffer, size_t size);
}  // namespace internal

// Tries to find the `adbkey` file in `android_user_dir` (copying from
// `~/.android/adbkey` if present there), returning an empty path if not found.
std::filesystem::path GetPrivateAdbKeyPath(const std::filesystem::path& android_user_dir);

// Ensures that the private `adbkey` file exists in `android_user_dir`.
// If missing, locates the host `adb` tool (searching `sdk_directory` and
// `PATH`) and invokes `adb keygen` into a temporary file under a file lock
// before atomically moving it into place.
absl::StatusOr<AdbKeyResult> EnsurePrivateAdbKey(const std::filesystem::path& android_user_dir,
                                                 const std::filesystem::path& sdk_directory = {});

// Creates an Android ADB public key string given the private key at `path`.
bool PubkeyFromPrivkey(const std::filesystem::path& path, std::string* out);

}  // namespace goldfish::adb
