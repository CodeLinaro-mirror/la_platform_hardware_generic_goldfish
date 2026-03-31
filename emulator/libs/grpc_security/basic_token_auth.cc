// Copyright (C) 2020 The Android Open Source Project
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
#include "android/emulation/control/basic_token_auth.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "grpc++/grpc++.h"

#include "android/emulation/control/auth_error_factory.h"

namespace android::emulation::control {

#define DEBUG 0

#if DEBUG
#define DD(...) ALOGD(__VA_ARGS__)
#else
#define DD(...)
#endif

DisableAccess BasicTokenAuth::no_access;
const std::string_view kSTUDIO = "android-studio";

namespace {
/**
 * Convert a Abseil status object to a gRPC status object.
 * (Note: gRPC will eventually migrate to abseil..)
 *
 * @param absl_status The Abseil status object to convert.
 * @return The equivalent Abseil status object.
 */
grpc::Status ConvertAbseilStatusToGrpcStatus(const absl::Status& absl_status) {
    return {static_cast<grpc::StatusCode>(absl_status.code()), std::string(absl_status.message())};
}
}  // namespace

BasicTokenAuth::BasicTokenAuth(std::string header, AllowList* list)
        : allow_list_(list), header_(std::move(header)) {}

BasicTokenAuth::~BasicTokenAuth() = default;

grpc::Status BasicTokenAuth::Process(const InputMetadata& auth_metadata,
                                     grpc::AuthContext* /*context*/,
                                     OutputMetadata* /*consumed_auth_metadata*/,
                                     OutputMetadata* /*response_metadata*/) {
    auto path = auth_metadata.find(kPath);

    if (path == auth_metadata.end()) {
        return {grpc::StatusCode::INTERNAL, "The metadata does not contain a path"};
    }

    const std::string_view uri(path->second.data(), path->second.length());

    // First lets see if we even need to validate this uri.
    if (!GetAllowList()->RequiresAuthentication(uri)) {
        return grpc::Status::OK;
    }

    auto header = auth_metadata.find(header_);
    if (header == auth_metadata.end()) {
        return ConvertAbseilStatusToGrpcStatus(AuthErrorFactory::AuthErrorMissingHeader(header_));
    }

    const std::string_view token(header->second.data(), header->second.length());
    if (!CanHandleToken(token)) {
        return ConvertAbseilStatusToGrpcStatus(
                AuthErrorFactory::AuthErrorNoValidatorForToken(uri, token));
    }

    auto auth = IsTokenValid(uri, token);
    return ConvertAbseilStatusToGrpcStatus(auth);
};

StaticTokenAuth::StaticTokenAuth(const std::string& token, std::string iss, AllowList* list)
        : BasicTokenAuth(kDefaultHeader, list)
        , static_token_(kDefaultBearer + token)
        , issuer_(std::move(iss)) {};

bool StaticTokenAuth::CanHandleToken(std::string_view token) {
    return token == static_token_;
}

absl::Status StaticTokenAuth::IsTokenValid(std::string_view path, std::string_view token) {
    if (!CanHandleToken(token)) {
        // This can be very verbose..
        DD("%s != %s", static_token_, token);
        return AuthErrorFactory::AuthErrorInvalidToken(token);
    }

    if (GetAllowList()->IsRed(issuer_, path)) {
        return AuthErrorFactory::AuthErrorNotOnAllowList(kSTUDIO, path,
                                                         GetAllowList()->GetSource());
    }

    return absl::OkStatus();
}

AnyTokenAuth::AnyTokenAuth(std::vector<std::unique_ptr<BasicTokenAuth>> validators,
                           AllowList* allowlist)
        : BasicTokenAuth(kDefaultHeader, allowlist), unique_validators_(std::move(validators)) {
    for (const auto& validator : unique_validators_) {
        validators_.push_back(validator.get());
    }
}

AnyTokenAuth::AnyTokenAuth(std::vector<BasicTokenAuth*> validators, AllowList* allowlist)
        : BasicTokenAuth(kDefaultHeader, allowlist), validators_(std::move(validators)) {}

bool AnyTokenAuth::CanHandleToken(std::string_view token) {
    if (validators_.empty()) {
        return true;
    }

    for (const auto& validator : validators_) {
        if (validator->CanHandleToken(token)) {
            return true;
        }
    }

    return false;
}

absl::Status AnyTokenAuth::IsTokenValid(std::string_view path, std::string_view token) {
    if (validators_.empty()) {
        return absl::OkStatus();
    }

    // This should only be called when at least one validator is willing to
    // process this token.
    for (const auto& validator : validators_) {
        if (validator->CanHandleToken(token)) {
            return validator->IsTokenValid(path, token);
        }
    }

    // Oh oh! How did we end up here??
    const std::string validators =
            std::accumulate(validators_.begin(), validators_.end(), std::string(),
                            [](auto str, const auto& validator) {
                                return std::move(str) + ", " + validator->Name();
                            });

    auto fatal_error = absl::StrFormat(
            "FATAL: No validator that can handle token. This "
            "should not happen, please file a bug including "
            "this information: Validation failure `validators: "
            "%s`, `path: %s`, `token: %s`",
            validators, path, token);

    LOG(ERROR) << "Internal error! " << fatal_error;

    // Should not happen, at least one validator should have been able
    // to handle the token.
    return absl::InternalError(fatal_error);
}

}  // namespace android::emulation::control
