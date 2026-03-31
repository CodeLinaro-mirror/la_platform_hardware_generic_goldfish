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
#pragma once
#include <grpcpp/grpcpp.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "absl/status/status.h"

#include "android/emulation/control/allow_list.h"
#include "android/emulation/control/basic_token_auth.h"
#include "android/emulation/control/jwk_directory_observer.h"
#include "tink/keyset_handle.h"

namespace android::emulation::control {

using Path = std::filesystem::path;

// A class that validates that the header:
//
// authorization: Bearer <token>
//
// is present and contains a valid JWT token.
//
// JWT tokens will be validated on the basis of a set of JWK files that can
// be found in the jkwsPath.
//
// Tokens must contain the following:
//
// An 'iss' field, that must be on the allow list.
// An 'aud' field matching the 'path' of the method invoked if
// the 'path' is not on the green list.
// The 'exp' field is not yet expired.
class JwtTokenAuth : public BasicTokenAuth {
  public:
    JwtTokenAuth(const Path& jwks_path, Path jwks_loaded_path, AllowList* list);
    ~JwtTokenAuth() override = default;

    bool CanHandleToken(std::string_view token) override;

    absl::Status IsTokenValid(std::string_view path, std::string_view token) override;

    std::string Name() override { return "JwtTokenAuth"; }

  private:
    void UpdateKeysetHandle(std::unique_ptr<crypto::tink::KeysetHandle> incoming_handle);

    static constexpr const std::string_view kDefaultBearer{"Bearer "};
    static constexpr const std::string_view kJwkExt{".jwk"};
    Path jwks_loaded_path_;
    std::mutex keyhandle_access_;
    absl::Status tink_initialized_;
    std::unique_ptr<crypto::tink::KeysetHandle> active_keyset_;
    std::unique_ptr<JwkDirectoryObserver> directory_observer_;
};
}  // namespace android::emulation::control
