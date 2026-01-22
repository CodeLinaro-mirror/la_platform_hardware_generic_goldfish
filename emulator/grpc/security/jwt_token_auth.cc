// Copyright (C) 2022 The Android Open Source Project
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
#include "android/emulation/control/jwt_token_auth.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

#include "android/emulation/control/auth_error_factory.h"
#include "tink/config/tink_config.h"
#include "tink/jwt/jwk_set_converter.h"
#include "tink/jwt/jwt_public_key_verify.h"
#include "tink/jwt/jwt_signature_config.h"
#include "tink/jwt/jwt_validator.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"

// #define DEBUG 1

#if DEBUG >= 1
#define DD(fmt, ...) printf("JwtTokenAuth: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::emulation::control {

namespace tink = crypto::tink;

JwtTokenAuth::JwtTokenAuth(const Path& jwks_path, Path jwks_loaded_path, AllowList* list)
        : BasicTokenAuth(kDefaultHeader, list), jwks_loaded_path_(std::move(jwks_loaded_path)) {
    tink_initialized_ = tink::TinkConfig::Register();
    if (tink_initialized_.ok()) {
        tink_initialized_ = tink::JwtSignatureRegister();
    }

    if (!jwks_loaded_path_.empty()) {
        LOG(INFO) << "The active JSON Web Key Sets can be found here: " << jwks_loaded_path_;
    }

    if (!tink_initialized_.ok()) {
        LOG(FATAL) << "Unable to initialize tink library. " << tink_initialized_.ToString();
    }
    directory_observer_ = std::make_unique<JwkDirectoryObserver>(
            jwks_path, [this](auto handle) { UpdateKeysetHandle(std::move(handle)); },
            [this](const auto& fname) {
                DD("Check %s  != %s", fname.c_str(), mJwksLoadedPath.c_str());
                return absl::EndsWith(fname.string(), kJwkExt) && fname != jwks_loaded_path_;
            });
}

void JwtTokenAuth::UpdateKeysetHandle(std::unique_ptr<crypto::tink::KeysetHandle> incoming_handle) {
    const std::lock_guard<std::mutex> lock(keyhandle_access_);
    if (tink_initialized_.ok()) active_keyset_ = std::move(incoming_handle);

    // Next we serialize the Jwkset to file.
    if (tink_initialized_.ok() && !jwks_loaded_path_.empty()) {
        DD("Updating active set %p", mActiveKeyset.get());
        std::string json_snippet = R"({"keys": []})";
        if (active_keyset_ != nullptr) {
            auto jwk_set = tink::JwkSetFromPublicKeysetHandle(*active_keyset_);
            if (!jwk_set.ok()) {
                DD("Cannot serialize jwk set %s", jwkSet.status().message().data());
            } else {
                json_snippet = jwk_set.value();
            }
        }

        std::ofstream out(jwks_loaded_path_, std::ios::trunc);
        out << json_snippet;
        out.close();
        DD("Updated %s with latest loaded keys to: %s", jwks_loaded_path_.c_str(),
           jsonSnippet.c_str());
    }
}

bool JwtTokenAuth::CanHandleToken(std::string_view token) {
    constexpr auto kOffset = kDefaultBearer.size();
    if (token.size() <= kOffset) {
        return false;
    }

    // See https://datatracker.ietf.org/doc/html/rfc7519#section-3
    // Base64url(JOSE Header) + '.' + Base64url(claims) + '.' +
    // Base64url(signature)
    return std::count(token.begin(), token.end(), '.') == 2;
}

absl::Status JwtTokenAuth::IsTokenValid(std::string_view path, std::string_view token) {
    if (!tink_initialized_.ok()) {
        return tink_initialized_;
    }

    constexpr auto kOffset = kDefaultBearer.size();
    if (token.size() <= kOffset) {
        return AuthErrorFactory::AuthErrorInvalidHeader(token, kDefaultBearer);
    }

    auto actual_token = absl::string_view(token.data() + kOffset, token.size() - kOffset);

    const std::lock_guard<std::mutex> lock(keyhandle_access_);
    if (!active_keyset_) {
        return AuthErrorFactory::AuthErrorNoKeySet(directory_observer_->Observes().string());
    }
    auto verify = active_keyset_->template GetPrimitive<tink::JwtPublicKeyVerify>();
    if (!verify.ok()) {
        DD("Token error: %s", verify.status().error_message().c_str());
        return verify.status();
    }

    DD("Make sure there is a proper signed jwt token.");
    auto validator = tink::JwtValidatorBuilder()
                             .ExpectIssuedInThePast()
                             .IgnoreIssuer()
                             .IgnoreAudiences()
                             .Build();

    if (!validator.ok()) {
        DD("Token validator error: %s", validator.status().error_message().c_str());
        return validator.status();
    }

    auto verified_jwt = (*verify)->VerifyAndDecode(actual_token, *validator);

    // Bad token, this means we have no way of doing anything useful.
    if (!verified_jwt.ok()) {
        DD("Bad token!: %s", verified_jwt.status().error_message().c_str());
        return verified_jwt.status();
    }

    auto iss = verified_jwt->GetIssuer();
    if (!iss.ok()) {
        return AuthErrorFactory::AuthErrorMissingIss(path);
    }

    // Green list, no need to validate aud..
    if (verified_jwt.ok() && GetAllowList()->IsAllowed(*iss, path)) {
        return absl::OkStatus();
    }

    // On the block list, go away!
    if (GetAllowList()->IsRed(*iss, path)) {
        return AuthErrorFactory::AuthErrorNotOnAllowList(*iss, path, GetAllowList()->GetSource());
    }

    auto audiences = verified_jwt->GetAudiences();
    if (!audiences.ok()) {
        DD("Token validator error: %s", audiences.status().error_message().c_str());
        return AuthErrorFactory::AuthErrorMissingAud(*iss, path);
    }

    for (const auto& aud : audiences.value()) {
        if (aud == path) {
            return absl::OkStatus();
        }
    }

    return AuthErrorFactory::AuthErrorMissingClaim(*iss, path);
}

}  // namespace android::emulation::control
