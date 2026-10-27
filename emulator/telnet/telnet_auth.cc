// Copyright (C) 2026 The Android Open Source Project
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
#include "telnet_auth.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/system.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"
#include "tink/subtle/random.h"

#ifdef _WIN32
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#endif

namespace goldfish::telnet {

namespace {

// Cap the token file size to 1KB to avoid OOM risk.
constexpr size_t kMaxTokenFileSize = 1024;
constexpr int kMaxProvisionAttempts = 10;
constexpr int kMinRetryBackoffMs = 10;
constexpr int kMaxRetryBackoffMs = 50;

absl::StatusOr<TelnetAuth::Token> GenerateToken(size_t entropy) {
    if (entropy == 0) {
        return absl::InvalidArgumentError(
                "Failed to generate emulator console authentication token: entropy size "
                "(0 bytes) must be at least 1 byte (recommended: 16 bytes).");
    }
    // Generate `entropy` bytes of entropy and encode it using Web-Safe Base64.
    // This avoids characters like '/' which cause issues in filenames and URLs.
    VLOG(1) << "Generating secure random token with " << entropy << " bytes of entropy.";
    const std::string random_bytes = crypto::tink::subtle::Random::GetRandomBytes(entropy);
    return TelnetAuth::Token(absl::WebSafeBase64Escape(random_bytes));
}

absl::Status WriteTokenPseudoSecurely(const std::filesystem::path& dest,
                                      const TelnetAuth::Token& token) {
    VLOG(1) << "Attempting to write token to file: " << dest;
    return android::base::file::CreatePrivateFileExclusive(dest, token.AsStringView());
}

}  // namespace

TelnetAuth::Token::Token(std::string_view token)
        : secret_(crypto::tink::util::SecretDataFromStringView(token)) {}

bool TelnetAuth::Token::SecureEquals(std::string_view other) const {
    return crypto::tink::util::SecretDataEquals(
            secret_, crypto::tink::util::SecretDataFromStringView(other));
}

std::string_view TelnetAuth::Token::AsStringView() const {
    return crypto::tink::util::SecretDataAsStringView(secret_);
}

absl::StatusOr<TelnetAuth::Token> TelnetAuth::LoadOrCreateToken(
        size_t entropy, const std::filesystem::path& token_path) {
    VLOG(1) << "Loading or creating telnet auth token.";
    absl::BitGen bitgen;
    absl::Status last_write_status = absl::OkStatus();

    for (int attempt = 0; attempt < kMaxProvisionAttempts; ++attempt) {
        absl::StatusOr<Token> token = ReadToken(token_path);
        if (token.ok()) {
            VLOG(1) << "Telnet auth token successfully loaded.";
            return token;
        }
        VLOG(1) << "Token file missing or unreadable (" << token.status()
                << "), attempting to provision.";

        // Attempt to write.
        absl::StatusOr<Token> new_token = GenerateToken(entropy);
        if (!new_token.ok()) {
            LOG(ERROR) << "Failed to generate emulator console authentication token for '"
                       << token_path.string() << "' (" << entropy
                       << " bytes of entropy): " << new_token.status();
            return new_token.status();
        }

        last_write_status = WriteTokenPseudoSecurely(token_path, *new_token);
        if (!last_write_status.ok()) {
            VLOG(1) << "WriteToken race lost or failed: " << last_write_status;
        }

        // Let's not all try again at the same time!
        // This also gives time for the rename to become visible to ReadToken.
        absl::SleepFor(
                absl::Milliseconds(absl::Uniform(bitgen, kMinRetryBackoffMs, kMaxRetryBackoffMs)));
    }

    absl::StatusOr<Token> final_token = ReadToken(token_path);
    if (final_token.ok()) {
        return final_token;
    }

    const std::string path_str = token_path.string();
    if (absl::IsNotFound(final_token.status()) && !last_write_status.ok()) {
        return absl::Status(
                last_write_status.code(),
                absl::StrCat("Failed to provision emulator console authentication token at '",
                             path_str, "' after ", kMaxProvisionAttempts, " attempts (",
                             absl::StripTrailingAsciiWhitespace(last_write_status.message()),
                             "). Verify that directory '", token_path.parent_path().string(),
                             "' exists and is writable."));
    }

    return absl::Status(final_token.status().code(),
                        absl::StrCat("Failed to load emulator console authentication token from '",
                                     path_str, "' after ", kMaxProvisionAttempts,
                                     " attempts: ", final_token.status().message()));
}

std::filesystem::path TelnetAuth::GetTokenPath() {
    const auto home = android::base::System::Get()->GetHomeDirectory();
    DCHECK(android::base::file::exists(home))
            << "Failed to resolve emulator console authentication token path: home directory '"
            << home.string() << "' does not exist.";
    return home / ".emulator_console_auth_token";
}

absl::StatusOr<TelnetAuth::Token> TelnetAuth::ReadToken(const std::filesystem::path& token_path) {
    VLOG(2) << "Reading token from: " << token_path;
    const std::string path_str = token_path.string();

    if (!android::base::file::exists(token_path)) {
        return absl::NotFoundError(absl::StrCat(
                "Failed to read emulator console authentication token file '", path_str,
                "': file does not exist. Verify the token path or restart the emulator to "
                "regenerate the authentication token file."));
    }

    if (!android::base::file::is_file(token_path)) {
        return absl::FailedPreconditionError(absl::StrCat(
                "Failed to read emulator console authentication token file '", path_str,
                "': path exists but is not a regular file. Remove or replace the non-file entry "
                "at '",
                path_str, "' and try again."));
    }

    if (!android::base::file::can_read(token_path)) {
        return absl::PermissionDeniedError(absl::StrCat(
                "Failed to read emulator console authentication token file '", path_str,
                "': insufficient permissions to read file. Grant read permissions for the current "
                "user and try again."));
    }

    const absl::StatusOr<android::base::StorageCapacity> file_size =
            android::base::file::file_size(token_path);
    if (!file_size.ok()) {
        return absl::Status(
                file_size.status().code(),
                absl::StrCat("Failed to determine size of emulator console authentication token "
                             "file '",
                             path_str, "': ", file_size.status().message(),
                             ". Check file permissions and filesystem state."));
    }

    if (file_size->Bytes() > kMaxTokenFileSize) {
        return absl::ResourceExhaustedError(absl::StrCat(
                "Failed to read emulator console authentication token file '", path_str,
                "': file size (", file_size->Bytes(), " bytes) exceeds maximum allowed size of ",
                kMaxTokenFileSize, " bytes. Replace the file with a valid token of at most ",
                kMaxTokenFileSize,
                " bytes or delete it and restart the emulator to regenerate one."));
    }

    // Read existing file after verifying it fits within kMaxTokenFileSize.
    const absl::StatusOr<std::string> raw_content =
            android::base::file::read_whole_file(token_path, /*binary=*/true);
    if (!raw_content.ok()) {
        VLOG(1) << "Failed to open token file at " << token_path << ": " << raw_content.status();
        return absl::Status(
                raw_content.status().code(),
                absl::StrCat("Failed to read emulator console authentication token file '",
                             path_str, "': ", raw_content.status().message(),
                             ". Ensure the file is readable and not locked by another process, "
                             "then try again."));
    }

    // Trim whitespace (newlines, spaces) which might be added by editors or echo.
    const std::string_view trimmed_content = absl::StripAsciiWhitespace(*raw_content);

    VLOG(2) << "Successfully read and trimmed " << trimmed_content.size()
            << " bytes from token file.";
    return TelnetAuth::Token(trimmed_content);
}

AuthStatus TelnetAuth::GetStatus(const std::filesystem::path& token_path) {
    if (!android::base::file::exists(token_path)) {
        VLOG(1) << "Token file does not exist at " << token_path << ", returning kRequired.";
        return AuthStatus::kRequired;
    }

    if (!android::base::file::is_file(token_path)) {
        VLOG(1) << "Token path " << token_path << " exists but is not a regular file.";
        return AuthStatus::kError;
    }

    if (!android::base::file::can_read(token_path)) {
        VLOG(1) << "Token file exists at " << token_path << " but is not readable.";
        return AuthStatus::kError;
    }

    const auto size = android::base::file::file_size(token_path);
    if (!size.ok()) {
        VLOG(1) << "Failed to get token file size for " << token_path << ": "
                << size.status().message();
        return AuthStatus::kError;
    }

    const auto size_bytes = size->Bytes();

    if (size_bytes > kMaxTokenFileSize) {
        VLOG(1) << "Token file exists at " << token_path << " but is too large to read (" << *size
                << " > " << kMaxTokenFileSize << " bytes).";
        return AuthStatus::kError;
    }

    if (size_bytes == 0) {
        VLOG(1) << "Token file exists at " << token_path << " but is empty, disabling security.";
        return AuthStatus::kDisabled;
    }

    VLOG(1) << "Token file exists and is readable at " << token_path << ", returning kRequired.";
    return AuthStatus::kRequired;
}

}  // namespace goldfish::telnet
