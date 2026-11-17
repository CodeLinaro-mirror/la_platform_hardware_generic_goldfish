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

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace android::emulation::control {

// A class that produces errors that are sensible to end users.
class AuthErrorFactory {
  public:
    /**
     * Produces a UnauthenticatedError when there are no active keysets
     *
     * @param jwk_path The path where the public keys should be stored.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorNoKeySet(std::string_view jwk_path);

    /**
     * Produces a UnauthenticatedError when we presented a bad header
     *
     * @param header The header that the user presented.
     * @param expected What we expected to be there.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorInvalidHeader(std::string_view header, std::string_view expected);

    /**
     * Produces a UnauthenticatedError when we presented a bad token.
     *
     * @param token The token that the user presented.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorInvalidToken(std::string_view token);

    /**
     * Produces a UnauthenticatedError when we are unable to process the token
     *
     * @param path The path that the user tried to access.
     * @param token The token that the user presented.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorNoValidatorForToken(std::string_view path, std::string_view token);

    /**
     * Produces a UnauthenticatedError when the security header is not present
     *
     * @param header The header that is missing
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorMissingHeader(std::string_view header);

    /**
     * Produces a UnauthenticatedError when an unidentified user
     * tries to access a path (iss field is missing)
     *
     * @param path The path that the user tried to access.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorMissingIss(std::string_view path);

    /**
     * Produces a PermissionDeniedError when a user tries to access a path
     * without having an "aud" entry
     *
     * @param iss The issuer. This is the user who will see the error
     * @param path The path that the user tried to access.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorMissingAud(std::string_view iss, std::string_view path);

    /**
     * Produces a PermissionDeniedError when a user tries to access a path
     * without having an "aud" claim to the path.
     *
     * * @param iss The issuer. This is the user who will see the error
     * @param path The path that the user tried to access.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorMissingClaim(std::string_view iss, std::string_view path);

    /**
     * Produces a PermissionDeniedError when a user tries to access a path
     * which is not on the allow list
     *
     * @param iss The issuer. This is the user who will see the error
     * @param path The path that the user tried to access.
     * @param allowListOrigin The location of the allowlist.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorNotOnAllowList(std::string_view iss, std::string_view path,
                                                std::string_view allow_list_origin);

    /**
     * Produces a PermissionDeniedError when a user tries to access a V2 endpoint
     * with a JWT that lacks any roles in its "roles" or "scope" claims.
     *
     * @param path The V2 gRPC endpoint path that the user tried to access.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorMissingRoles(std::string_view path);

    /**
     * Produces a PermissionDeniedError when none of the roles presented in a JWT
     * grant access to the requested V2 endpoint.
     *
     * @param roles The roles extracted from the JWT's "roles" and/or "scope" claims.
     * @param path The V2 gRPC endpoint path that the user tried to access.
     * @param allow_list_origin The location of the allowlist.
     * @return An absl::Status indicating failure of the operation.
     */
    static absl::Status AuthErrorInsufficientRoles(absl::Span<const std::string> roles,
                                                   std::string_view path,
                                                   std::string_view allow_list_origin);
};

}  // namespace android::emulation::control
