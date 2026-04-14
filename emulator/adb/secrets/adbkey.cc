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

#include "goldfish/adb/adbkey.h"

#include <openssl/base.h>
#include <openssl/base64.h>  // for EVP_EncodeBlock, EVP_Encod...
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/nid.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "absl/log/log.h"

#include "android/base/system.h"
#include "goldfish/file/file.h"

namespace goldfish::adb {

namespace internal {

namespace {

using android::base::System;

constexpr const char* kPrivateKeyFileName = "adbkey";

// Better safe than sorry.
static_assert(kAndroidPubkeyModulusSize % 4 == 0,
              "RSA modulus size must be multiple of the word size!");

// Size of the RSA modulus in words.
constexpr const int kAndroidPubkeyModulusSizeWords = kAndroidPubkeyModulusSize / 4;

std::string GetUserInfo() {
    std::string hostname = android::base::System::GetEnvironmentVariable("HOSTNAME");
    if (hostname.empty()) {
        hostname = "unknown";
    }

    std::string username;
    if (getenv("LOGNAME")) {
        username = getenv("LOGNAME");
    }
    if (username.empty()) {
        hostname = "unknown";
    }
    return " " + username + "@" + hostname;
}

std::shared_ptr<RSA> ReadKeyFile(const fs::path& file) {
    const std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(file.string().c_str(), "r"),
                                                      fclose);
    if (!fp) {
        LOG(ERROR) << "Failed to open rsa file: " << file;
        return nullptr;
    }

    RSA* key = RSA_new();
    if (!PEM_read_RSAPrivateKey(fp.get(), &key, nullptr, nullptr)) {
        LOG(ERROR) << "Failed to read rsa key";
        RSA_free(key);
        return nullptr;
    }

    return {key, RSA_free};
}

// This file implements encoding and decoding logic for Android's custom RSA
// public key binary format. Public keys are stored as a sequence of
// little-endian 32 bit words. Note that Android only supports little-endian
// processors, so we don't do any byte order conversions when parsing the binary
// struct.
using RSAPublicKey = struct RSAPublicKey {
    // Modulus length. This must be kAndroidPubkeyModulusSize.
    uint32_t modulus_size_words;

    // Precomputed montgomery parameter: -1 / n[0] mod 2^32
    uint32_t n0inv;

    // RSA modulus as a little-endian array.
    uint8_t modulus[kAndroidPubkeyModulusSize];

    // Montgomery parameter R^2 as a little-endian array of little-endian words.
    uint8_t rr[kAndroidPubkeyModulusSize];

    // RSA modulus: 3 or 65537
    uint32_t exponent;
};

// From ${AOSP}/system/core/adb/client/auth.cpp
bool CalculatePublicKey(std::string* out, RSA* private_key) {
    uint8_t binary_key_data[kAndroidPubkeyEncodedSize];
    if (!AndroidPubkeyEncode(private_key, binary_key_data, sizeof(binary_key_data))) {
        LOG(ERROR) << "Failed to convert to public key";
        return false;
    }

    size_t expected_length;
    if (!EVP_EncodedLength(&expected_length, sizeof(binary_key_data))) {
        LOG(ERROR) << "Public key too large to base64 encode";
        return false;
    }

    out->resize(expected_length);
    const size_t actual_length = EVP_EncodeBlock(reinterpret_cast<uint8_t*>(out->data()),
                                                 binary_key_data, sizeof(binary_key_data));
    out->resize(actual_length);
    out->append(GetUserInfo());
    return true;
}

// Get adbkey path, return "" if failed
// adb_key_file_name could be "adbkey" or "adbkey.pub"
fs::path GetAdbKeyPath(const fs::path& android_user_dir, const fs::path& adb_key_file_name) {
    fs::path adb_key_path = android_user_dir / adb_key_file_name;
    if (android::base::file::is_file(adb_key_path) && android::base::file::can_read(adb_key_path)) {
        return adb_key_path;
    }

    auto home = System::Get()->GetHomeDirectory();
    if (home.empty()) {
        home = System::Get()->GetTempDir();
        if (home.empty()) {
            home = "/tmp";
        }
    }

    auto guessed_src_adb_key_pub = home / ".android" / adb_key_file_name;
    android::base::file::cp_file(adb_key_path, guessed_src_adb_key_pub).IgnoreError();

    if (android::base::file::is_file(adb_key_path) && android::base::file::can_read(adb_key_path)) {
        return adb_key_path;
    }
    return "";
}

}  // namespace

bool AndroidPubkeyDecode(const uint8_t* key_buffer, size_t size, RSA** key) {
    // Check |size| is large enough and the modulus size is correct.
    if (size < sizeof(RSAPublicKey)) {
        LOG(ERROR) << "WARNING: adbkey decode failed.";
        return false;
    }
    RSAPublicKey key_struct;
    memcpy(&key_struct, key_buffer, sizeof(key_struct));

    if (key_struct.modulus_size_words != kAndroidPubkeyModulusSizeWords) {
        LOG(ERROR) << "WARNING: adbkey decode failed.";
        return false;
    }

    // Read the modulus and exponent.
    bssl::UniquePtr<BIGNUM> n(BN_le2bn(key_struct.modulus, kAndroidPubkeyModulusSize, nullptr));
    bssl::UniquePtr<BIGNUM> e(BN_new());
    if (!n || !e || !BN_set_word(e.get(), key_struct.exponent)) {
        LOG(ERROR) << "WARNING: adbkey decode failed.";
        return false;
    }

    // TODO(davidben): After we're sure the BoringSSL update has stuck, switch
    // this to RSA_new_public_key, which is a bit less tedious. For now, use the
    // older APIs to avoid a build break if it gets temporarily reverted.
    bssl::UniquePtr<RSA> new_key(RSA_new());
    if (!new_key || !RSA_set0_key(new_key.get(), n.get(), e.get(), /*d=*/nullptr)) {
        LOG(ERROR) << "WARNING: adbkey decode failed.";
        return false;
    }
    // RSA_set0_key takes ownership on success.
    (void)n.release();  // NOLINT
    (void)e.release();  // NOLINT

    *key = new_key.release();
    return true;
}

bool AndroidPubkeyEncode(const RSA* key, uint8_t* key_buffer, size_t size) {
    if (sizeof(RSAPublicKey) > size || RSA_size(key) != kAndroidPubkeyModulusSize) {
        return false;
    }

    // Store the modulus size.
    RSAPublicKey key_struct;
    key_struct.modulus_size_words = kAndroidPubkeyModulusSizeWords;

    // Compute and store n0inv = -1 / N[0] mod 2^32.
    const bssl::UniquePtr<BN_CTX> ctx(BN_CTX_new());
    const bssl::UniquePtr<BIGNUM> r32(BN_new());
    const bssl::UniquePtr<BIGNUM> n0inv(BN_new());
    if (!ctx || !r32 || !n0inv || !BN_set_bit(r32.get(), 32) ||
        !BN_mod(n0inv.get(), RSA_get0_n(key), r32.get(), ctx.get()) ||
        !BN_mod_inverse(n0inv.get(), n0inv.get(), r32.get(), ctx.get()) ||
        !BN_sub(n0inv.get(), r32.get(), n0inv.get())) {
        return false;
    }
    key_struct.n0inv = static_cast<uint32_t>(BN_get_word(n0inv.get()));

    // Store the modulus.
    if (!BN_bn2le_padded(key_struct.modulus, kAndroidPubkeyModulusSize, RSA_get0_n(key))) {
        return false;
    }

    // Compute and store rr = (2^(rsa_size)) ^ 2 mod N.
    const bssl::UniquePtr<BIGNUM> rr(BN_new());
    if (!rr || !BN_set_bit(rr.get(), kAndroidPubkeyModulusSize * 8) ||
        !BN_mod_sqr(rr.get(), rr.get(), RSA_get0_n(key), ctx.get()) ||
        !BN_bn2le_padded(key_struct.rr, kAndroidPubkeyModulusSize, rr.get())) {
        return false;
    }

    // Store the exponent.
    key_struct.exponent = static_cast<uint32_t>(BN_get_word(RSA_get0_e(key)));
    memcpy(key_buffer, &key_struct, sizeof(key_struct));
    return true;
}

bool TestOnlyAdbAuthKeygen(const fs::path& file) {
    const std::unique_ptr<BIGNUM, decltype(&BN_free)> exponent(BN_new(), BN_free);
    if (!exponent) {
        LOG(WARNING) << "Failed to allocate key";
        return false;
    }
    BN_set_word(exponent.get(), RSA_F4);

    const std::unique_ptr<RSA, decltype(&RSA_free)> rsa(RSA_new(), RSA_free);
    if (!rsa) {
        LOG(WARNING) << "Failed to allocate key";
        return false;
    }
    RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr);

    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey) {
        LOG(WARNING) << "Failed to allocate key";
        return false;
    }
    EVP_PKEY_set1_RSA(pkey.get(), rsa.get());

    std::unique_ptr<FILE, decltype(&fclose)> fp(std::fopen(file.string().c_str(), "w"), fclose);
    if (!fp) {
        LOG(WARNING) << "Failed to open " << file.string();
        return false;
    }

    if (!PEM_write_PrivateKey(fp.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
        LOG(WARNING) << "Failed to write key";
        return false;
    }

    fp.reset();
    if (auto s = android::base::file::chmod(file, 0777); !s.ok()) {
        LOG(WARNING) << "Failed to change key permissions: " << s;
        return false;
    }

    return true;
}

}  // namespace internal

bool PubkeyFromPrivkey(const fs::path& path, std::string* out) {
    const std::shared_ptr<RSA> privkey = internal::ReadKeyFile(path);
    if (!privkey) {
        return false;
    }
    return internal::CalculatePublicKey(out, privkey.get());
}

fs::path GetPrivateAdbKeyPath(const fs::path& android_user_dir) {
    return internal::GetAdbKeyPath(android_user_dir, internal::kPrivateKeyFileName);
}

}  // namespace goldfish::adb
