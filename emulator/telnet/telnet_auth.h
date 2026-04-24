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
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "absl/status/statusor.h"

#include "tink/util/secret_data.h"

namespace goldfish::telnet {

/**
 * @brief Enumeration representing the state of telnet authentication enforcement.
 *
 * The enforcement status is determined by the existence and content of the
 * authentication token file (typically `~/.emulator_console_auth_token`).
 */
enum class AuthStatus : uint8_t {
    /**
     * @brief Authentication is explicitly disabled.
     *
     * This state occurs when the token file exists but is empty (0 bytes).
     * When disabled, the telnet console will allow connections without
     * challenging for an authentication token.
     */
    kDisabled,

    /**
     * @brief Authentication is required.
     *
     * This is the default state. It occurs when the token file contains data
     * or when the token file does not exist yet. If the file is missing, it
     * should be provisioned (e.g., via @ref TelnetAuth::LoadOrCreateToken())
     * before the first authentication challenge.
     */
    kRequired,

    /**
     * @brief An error occurred while determining the status.
     *
     * This state occurs if the token file is inaccessible due to permission
     * issues, or if the file exceeds the internal size limit of 1KB (1024 bytes).
     * In this state, the console should generally deny access as a safety
     * precaution.
     */
    kError
};

/**
 * @class TelnetAuth
 * @brief Manages the lifecycle and access of the telnet authentication token.
 *
 * This class provides a thread-safe and process-safe mechanism for managing the
 * authentication token used to secure the emulator's telnet interface. It handles
 * concurrent access from multiple emulator instances, ensuring they all
 * reach consensus on the same token value.
 *
 * @section security Security Warning
 * The telnet-based interface is a legacy component and is **not considered
 * secure** for modern production environments. While this class implements
 * basic protections (filesystem permissions, random tokens), the underlying
 * telnet protocol transmits data in plain text.
 *
 * @note The token is stored in the user's home directory as
 * `.emulator_console_auth_token`.
 */
class TelnetAuth {
  public:
    /**
     * @brief Represents a secure authentication token.
     *
     * This struct wraps the sensitive token data in a secure container
     * (crypto::tink::util::SecretData) to prevent accidental leakage and
     * provides secure comparison methods.
     */
    struct Token {
        /**
         * @brief Constructs a Token from a string view.
         * @param token The raw token string.
         */
        explicit Token(std::string_view token);

        /**
         * @brief Securely compares this token with another string.
         *
         * Uses a constant-time comparison to prevent timing attacks.
         *
         * @param other The string to compare against.
         * @return true if the tokens match, false otherwise.
         */
        bool SecureEquals(std::string_view other) const;

        /**
         * @brief Returns a string view representation of the token.
         *
         * @note The returned view points to data owned by the token.
         * @return A string_view of the secret data.
         */
        std::string_view AsStringView() const;

      private:
        crypto::tink::util::SecretData secret_;
    };

    /**
     * @brief Returns the absolute path to the authentication token file.
     *
     * The path is typically `$HOME/.emulator_console_auth_token`.
     *
     * @return The filesystem path where the token is (or will be) stored.
     */
    static std::filesystem::path GetTokenPath();

    /**
     * @brief Evaluates the current authentication enforcement status.
     *
     * Inspects the token file on disk to determine if the telnet console
     * should require a password.
     *
     * @li Returns @ref AuthStatus::kDisabled if the file exists but is empty.
     * @li Returns @ref AuthStatus::kRequired if the file is missing or contains data.
     * @li Returns @ref AuthStatus::kError if the file is too large (>1KB) or unreadable.
     *
     * @note This method is read-only and will not modify the filesystem.
     *
     * @return The current @ref AuthStatus.
     */
    static AuthStatus GetStatus();

    /**
     * @brief Reads the authentication token from the filesystem.
     *
     * Loads the token string from the path returned by @ref GetTokenPath().
     * Leading and trailing ASCII whitespace is automatically stripped from
     * the returned value.
     *
     * @return The trimmed token string on success.
     * @retval absl::NotFoundError if the token file does not exist.
     * @retval absl::InternalError if the file is too large (>1KB) or cannot be opened.
     */
    static absl::StatusOr<Token> ReadToken();

    /**
     * @brief Ensures a valid authentication token exists and returns its value.
     *
     * This method implements a "First-Writer-Wins" strategy that is safe for
     * concurrent access by multiple emulator processes. If multiple processes
     * attempt to provision a token at the same time, only one will succeed in
     * writing its generated token, and the others will automatically fall back
     * to reading the winner's token.
     *
     * The provisioning flow:
     * 1. Attempt to read an existing token via @ref ReadToken().
     * 2. If @ref ReadToken() fails (e.g., file missing or unreadable), generate
     *    a new secure random token using Web-Safe Base64 encoding.
     * 3. Attempt to write the new token to disk with exclusive access.
     *    - On POSIX: Uses 0600 permissions (owner read/write only).
     *    - On Windows: Uses restricted ACLs (Owner and System only).
     * 4. If the write fails (e.g., another process wrote it first), retry the
     *    read/write cycle with a random backoff (10-50ms).
     *
     * The method will attempt this cycle up to 10 times before returning the
     * result of the final @ref ReadToken() attempt.
     *
     * @param entropy The number of bytes of entropy to use for new tokens.
     *                Defaults to 16 bytes, which results in a 22-character
     *                Web-Safe Base64 string.
     *
     * @return The existing or newly provisioned authentication token.
     * @retval absl::Status Any error encountered during generation or disk I/O.
     */
    static absl::StatusOr<Token> LoadOrCreateToken(size_t entropy = 16);
};

}  // namespace goldfish::telnet
