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
#include <filesystem>
#include <string>

namespace goldfish::adb {

namespace fs = std::filesystem;

namespace internal {
// Size of an RSA modulus such as an encrypted block or a signature.
constexpr const size_t kAndroidPubkeyModulusSize = 2048 / 8;
// Size of an encoded RSA key.
constexpr const size_t kAndroidPubkeyEncodedSize =
        ((3 * sizeof(uint32_t)) + (2 * kAndroidPubkeyModulusSize));

bool AndroidPubkeyDecode(const uint8_t* key_buffer, size_t size, RSA** key);
bool AndroidPubkeyEncode(const RSA* key, uint8_t* key_buffer, size_t size);

bool TestOnlyAdbAuthKeygen(const fs::path& file);
}  // namespace internal

// Tries to find the "adbkey" file, returning "" if not found
std::filesystem::path GetPrivateAdbKeyPath(const fs::path& android_user_dir);

// Creates a public key given the private key.
// |path| Path to the adb private key.
// |out| string receiving the public key.
bool PubkeyFromPrivkey(const std::filesystem::path& path, std::string* out);

}  // namespace goldfish::adb
