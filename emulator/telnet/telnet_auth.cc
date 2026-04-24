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
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
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

absl::StatusOr<TelnetAuth::Token> GenerateToken(size_t entropy) {
    // Generate 16 bytes of entropy and encode it using Web-Safe Base64.
    // This avoids characters like '/' which cause issues in filenames and URLs.
    VLOG(1) << "Generating secure random token with " << entropy << " bytes of entropy.";
    auto random_bytes_res = crypto::tink::subtle::Random::GetRandomBytes(entropy);
    return TelnetAuth::Token(absl::WebSafeBase64Escape(random_bytes_res));
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

absl::StatusOr<TelnetAuth::Token> TelnetAuth::LoadOrCreateToken(size_t entropy,
                                                                const fs::path& token_path) {
    VLOG(1) << "Loading or creating telnet auth token.";
    int max_attempts = 10;

    absl::BitGen bitgen;

    while (max_attempts-- > 0) {
        auto token = ReadToken(token_path);
        if (token.ok()) {
            VLOG(1) << "Telnet auth token successfully loaded.";
            return token;
        }

        VLOG(1) << "Token file missing or unreadable (" << token.status()
                << "), attempting to provision.";

        // Attempt to write.
        auto new_token_res = GenerateToken(entropy);
        if (new_token_res.ok()) {
            auto status = WriteTokenPseudoSecurely(token_path, *new_token_res);
            if (!status.ok()) {
                VLOG(1) << "WriteToken race lost or failed: " << status;
            }
        } else {
            LOG(ERROR) << "Failed to generate secure token: " << new_token_res.status();
            return new_token_res.status();
        }

        // Let's not all try again at the same time!
        // This also gives time for the rename to become visible to ReadToken.
        absl::SleepFor(absl::Milliseconds(absl::Uniform(bitgen, 10, 50)));
    }

    return ReadToken(token_path);
}

std::filesystem::path TelnetAuth::GetTokenPath() {
    const auto home = android::base::System::Get()->GetHomeDirectory();
    DCHECK(android::base::file::exists(home))
            << "Home directory does not exist, we cannot deal with that";
    return home / ".emulator_console_auth_token";
}

absl::StatusOr<TelnetAuth::Token> TelnetAuth::ReadToken(const fs::path& token_path) {
    VLOG(2) << "Reading token from: " << token_path;

    if (!android::base::file::exists(token_path)) {
        return absl::NotFoundError(
                absl::StrCat("The token file:", token_path.string(), " does not exist."));
    }

    if (android::base::file::file_size(token_path).value_or(0) > kMaxTokenFileSize) {
        return absl::InternalError(absl::StrCat(
                "The emulator console authentication token file at '", token_path.string(),
                "' is too large to read. A token can contain at most ", kMaxTokenFileSize,
                " bytes. Please check the file and ensure it contains a valid token."));
    }

    // Read existing file with a size limit.
    std::ifstream ifs(token_path, std::ios::binary);
    if (!ifs) {
        VLOG(1) << "Failed to open token file at " << token_path;
        return absl::InternalError(
                absl::StrCat("Failed to open the emulator console authentication token file at '",
                             token_path.string(),
                             "'. Ensure the file is readable and has correct permissions."));
    }

    std::string content;
    content.resize(kMaxTokenFileSize);
    ifs.read(content.data(), kMaxTokenFileSize);
    if (ifs.fail() && !ifs.eof()) {
        return absl::InternalError("Failed to read token data.");
    }
    content.resize(ifs.gcount());

    // Trim whitespace (newlines, spaces) which might be added by editors or echo.
    content = std::string(absl::StripAsciiWhitespace(content));

    VLOG(2) << "Successfully read and trimmed " << content.size() << " bytes from token file.";
    return TelnetAuth::Token(content);
}

AuthStatus TelnetAuth::GetStatus(const fs::path& token_path) {
    if (!android::base::file::exists(token_path)) {
        VLOG(1) << "Token file does not exist at " << token_path << ", returning kRequired.";
        return AuthStatus::kRequired;
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

    if (*size > kMaxTokenFileSize) {
        VLOG(1) << "Token file exists at " << token_path << " but is too large to read." << *size
                << " > " << kMaxTokenFileSize;
        return AuthStatus::kError;
    }

    if (*size == 0) {
        VLOG(1) << "Token file exists at " << token_path << " but is empty, disabling security.";
        return AuthStatus::kDisabled;
    }

    VLOG(1) << "Token file exists and is readable at " << token_path << ", returning kRequired.";
    return AuthStatus::kRequired;
}

}  // namespace goldfish::telnet
