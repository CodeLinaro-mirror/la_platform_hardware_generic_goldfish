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
#include <openssl/base64.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"

#include "android/base/system.h"
#include "android/process/command.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_lock.h"

namespace goldfish::adb {

namespace fs = std::filesystem;

namespace internal {
namespace {

using android::base::System;

constexpr std::string_view kPrivateKeyFileName = "adbkey";
constexpr std::string_view kPublicKeyFileName = "adbkey.pub";

// Better safe than sorry.
static_assert(kAndroidPubkeyModulusSize % 4 == 0,
              "RSA modulus size must be multiple of the word size!");

// Size of the RSA modulus in words.
constexpr int kAndroidPubkeyModulusSizeWords = kAndroidPubkeyModulusSize / 4;

fs::path NextTempKeyPath(const fs::path& dir, std::string_view base_name) {
    static std::atomic<uint64_t> seq{0};
    return dir / absl::StrCat(".", base_name, ".tmp.", System::GetCurrentProcessPid(), ".",
                              seq.fetch_add(1, std::memory_order_relaxed));
}

std::string FirstNonEmptyEnv(std::initializer_list<std::string_view> names,
                             std::string_view fallback) {
    auto* sys = System::Get();
    for (std::string_view name : names) {
        if (std::string val = sys->EnvGet(name); !val.empty()) {
            return val;
        }
    }
    return std::string(fallback);
}

std::string GetUserInfo() {
    return absl::StrCat(" ", FirstNonEmptyEnv({"LOGNAME", "USER", "USERNAME"}, "unknown"), "@",
                        FirstNonEmptyEnv({"HOSTNAME", "COMPUTERNAME"}, "unknown"));
}

std::string_view Unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bssl::UniquePtr<RSA> ReadKeyFile(const fs::path& file) {
    auto contents = android::base::file::read_whole_file(file, /*binary=*/true);
    if (!contents.ok()) {
        LOG(ERROR) << "Failed to open rsa file: " << file << ": " << contents.status();
        return nullptr;
    }
    if (contents->empty()) {
        LOG(ERROR) << "Empty rsa file: " << file;
        return nullptr;
    }

    const bssl::UniquePtr<BIO> bio(
            BIO_new_mem_buf(contents->data(), static_cast<int>(contents->size())));
    if (!bio) {
        LOG(ERROR) << "Failed to allocate BIO for rsa file: " << file;
        return nullptr;
    }

    bssl::UniquePtr<RSA> key(PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr));
    if (!key) {
        LOG(ERROR) << "Failed to read rsa key";
        return nullptr;
    }

    return key;
}

// This file implements encoding and decoding logic for Android's custom RSA
// public key binary format. Public keys are stored as a sequence of
// little-endian 32 bit words. Note that Android only supports little-endian
// processors, so we don't do any byte order conversions when parsing the binary
// struct.
struct RSAPublicKey {
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

    size_t expected_length = 0;
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

fs::path FindAdbExecutable(const fs::path& sdk_directory) {
#ifdef _WIN32
    constexpr std::string_view kAdbBinaryName = "adb.exe";
    constexpr char kPathListSeparator = ';';
#else
    constexpr std::string_view kAdbBinaryName = "adb";
    constexpr char kPathListSeparator = ':';
#endif
    auto is_executable_file = [](const fs::path& candidate) {
        return !candidate.empty() && android::base::file::is_file(candidate) &&
               android::base::file::can_exec(candidate);
    };
    if (!sdk_directory.empty()) {
        if (fs::path candidate = sdk_directory / "platform-tools" / kAdbBinaryName;
            is_executable_file(candidate)) {
            return candidate;
        }
    }

    const std::string path_env = System::Get()->EnvGet("PATH");
    for (std::string_view entry : absl::StrSplit(path_env, kPathListSeparator, absl::SkipEmpty())) {
        const std::string_view dir = Unquote(entry);
        if (dir.empty()) {
            continue;
        }
        if (fs::path candidate = fs::path(dir) / kAdbBinaryName; is_executable_file(candidate)) {
            return candidate;
        }
    }

    return {};
}

}  // namespace

bool AndroidPubkeyDecode(const uint8_t* key_buffer, size_t size, RSA** key) {
    // Check `size` is large enough and the modulus size is correct.
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

}  // namespace internal

bool PubkeyFromPrivkey(const fs::path& path, std::string* out) {
    const bssl::UniquePtr<RSA> privkey = internal::ReadKeyFile(path);
    if (!privkey) {
        return false;
    }
    return internal::CalculatePublicKey(out, privkey.get());
}

fs::path GetPrivateAdbKeyPath(const fs::path& android_user_dir) {
    if (android_user_dir.empty()) {
        return {};
    }

    const fs::path privkey_path = android_user_dir / internal::kPrivateKeyFileName;
    if (android::base::file::is_file(privkey_path) && android::base::file::can_read(privkey_path)) {
        return privkey_path;
    }
    if (android::base::file::exists(privkey_path) || android::base::file::is_link(privkey_path)) {
        return {};
    }

    const fs::path home = android::base::System::Get()->GetHomeDirectory();
    if (home.empty()) {
        return {};
    }

    const fs::path default_src = home / ".android" / internal::kPrivateKeyFileName;
    // If `android_user_dir` already resolves to `~/.android`, `default_src` is
    // the same missing file we just checked above, so there is no fallback key
    // to copy from.
    if (default_src.lexically_normal() == privkey_path.lexically_normal()) {
        return {};
    }

    if (android::base::file::is_file(default_src) && android::base::file::can_read(default_src)) {
        android::base::file::mkdir_recursive(android_user_dir, 0750).IgnoreError();
        const fs::path tmp_dst =
                internal::NextTempKeyPath(android_user_dir, internal::kPrivateKeyFileName);
        absl::Cleanup cleanup_tmp = [&tmp_dst]() {
            android::base::file::rm(tmp_dst).IgnoreError();
        };
        if (android::base::file::cp_file(default_src, tmp_dst, /*overwrite=*/true).ok()) {
            android::base::file::chmod(tmp_dst, 0600).IgnoreError();
            if (android::base::file::mv_file(tmp_dst, privkey_path,
                                             /*fallback_to_copy_rm=*/false)
                        .ok()) {
                return privkey_path;
            }
        }
    }
    return {};
}

absl::StatusOr<AdbKeyResult> EnsurePrivateAdbKey(const fs::path& android_user_dir,
                                                 const fs::path& sdk_directory) {
    if (android_user_dir.empty()) {
        return absl::InvalidArgumentError("android_user_dir must not be empty");
    }

    if (fs::path existing = GetPrivateAdbKeyPath(android_user_dir); !existing.empty()) {
        return AdbKeyResult{.private_key_path = std::move(existing), .newly_created = false};
    }

    const fs::path privkey_path = android_user_dir / internal::kPrivateKeyFileName;
    auto check_unreadable_existing_key = [&privkey_path]() -> absl::Status {
        if (android::base::file::exists(privkey_path) ||
            android::base::file::is_link(privkey_path)) {
            return absl::PermissionDeniedError(absl::StrCat(
                    privkey_path.string(),
                    " exists but is not readable; fix its permissions or ownership instead of "
                    "regenerating"));
        }
        return absl::OkStatus();
    };
    RETURN_IF_ERROR(check_unreadable_existing_key());

    const fs::path adb_exe = internal::FindAdbExecutable(sdk_directory);
    if (adb_exe.empty()) {
        const std::string sdk_hint = sdk_directory.empty()
                                             ? "(no SDK directory)"
                                             : (sdk_directory / "platform-tools").string();
        return absl::NotFoundError(
                absl::StrCat("adb not found in ", sdk_hint,
                             " or PATH; install Android SDK Platform-Tools ('adb') so the emulator "
                             "can create adbkey"));
    }

    RETURN_IF_ERROR(android::base::file::mkdir_recursive(android_user_dir, 0750));

    ASSIGN_OR_RETURN(auto lock,
                     android::base::file::ScopedFileLock::Acquire(privkey_path, absl::Seconds(10)));

    // Re-check under the lock in case another emulator instance generated the key
    // while we were waiting.
    if (fs::path existing = GetPrivateAdbKeyPath(android_user_dir); !existing.empty()) {
        return AdbKeyResult{.private_key_path = std::move(existing), .newly_created = false};
    }
    RETURN_IF_ERROR(check_unreadable_existing_key());

    const fs::path tmp_privkey =
            internal::NextTempKeyPath(android_user_dir, internal::kPrivateKeyFileName);
    fs::path tmp_pubkey = tmp_privkey;
    tmp_pubkey += ".pub";

    absl::Cleanup cleanup_tmp = [&tmp_privkey, &tmp_pubkey]() {
        android::base::file::rm(tmp_privkey).IgnoreError();
        android::base::file::rm(tmp_pubkey).IgnoreError();
    };

    RETURN_IF_ERROR(android::base::Command::LaunchAndCapture(
                            adb_exe, {"keygen", tmp_privkey.string()}, absl::Seconds(10))
                            .status());

    if (!android::base::file::is_file(tmp_privkey) || !android::base::file::can_read(tmp_privkey)) {
        return absl::InternalError(
                absl::StrCat("adb keygen completed without creating readable private key: ",
                             tmp_privkey.string()));
    }

    android::base::file::chmod(tmp_privkey, 0600).IgnoreError();

    if (android::base::file::is_file(tmp_pubkey)) {
        const fs::path pubkey_path = android_user_dir / internal::kPublicKeyFileName;
        RETURN_IF_ERROR(android::base::file::mv_file(tmp_pubkey, pubkey_path,
                                                     /*fallback_to_copy_rm=*/false));
    }
    RETURN_IF_ERROR(
            android::base::file::mv_file(tmp_privkey, privkey_path, /*fallback_to_copy_rm=*/false));

    return AdbKeyResult{.private_key_path = privkey_path, .newly_created = true};
}

}  // namespace goldfish::adb
