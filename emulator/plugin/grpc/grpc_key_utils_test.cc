// Copyright 2026 The Android Open Source Project
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

#include "goldfish/grpc/grpc_key_utils.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/emulation/control/allow_list.h"
#include "android/emulation/control/jwt_token_auth.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/file/file.h"
#include "tink/jwt/jwk_set_converter.h"
#include "tink/jwt/jwt_key_templates.h"
#include "tink/jwt/jwt_public_key_sign.h"
#include "tink/jwt/jwt_signature_config.h"
#include "tink/jwt/raw_jwt.h"
#include "tink/keyset_handle.h"

namespace fs = std::filesystem;
namespace tink = crypto::tink;
using ::absl_testing::IsOk;
using ::android::base::TestTempDir;
using ::android::emulation::control::AllowList;
using ::android::emulation::control::JwtTokenAuth;

namespace goldfish::grpc {

namespace {

struct GeneratedTestKey {
    std::unique_ptr<tink::KeysetHandle> private_handle;
    std::string public_jwk_set;
};

absl::StatusOr<GeneratedTestKey> GenerateTestKey() {
    auto status = tink::JwtSignatureRegister();
    if (!status.ok()) return status;

    auto private_handle = tink::KeysetHandle::GenerateNew(tink::JwtEs512Template());
    if (!private_handle.ok()) return private_handle.status();

    auto public_handle = (*private_handle)->GetPublicKeysetHandle();
    if (!public_handle.ok()) return public_handle.status();

    auto jwk_set = tink::JwkSetFromPublicKeysetHandle(**public_handle);
    if (!jwk_set.ok()) return jwk_set.status();

    return GeneratedTestKey{
        .private_handle = *std::move(private_handle),
        .public_jwk_set = *std::move(jwk_set),
    };
}

absl::StatusOr<std::string> GenerateValidJwkString() {
    auto key = GenerateTestKey();
    if (!key.ok()) return key.status();
    return std::move(key->public_jwk_set);
}

TEST(GrpcCustomKeyTest, EmptyList_ReturnsOk) {
    TestTempDir test_dir("jwk_empty_test");
    auto status = ProcessCustomJwtKeys({}, test_dir.path);
    EXPECT_TRUE(status.ok());
}

TEST(GrpcCustomKeyTest, ValidJwkJson_WritesCustomKeyFile) {
    TestTempDir test_dir("jwk_valid_test");
    fs::path temp_dir = test_dir.path;

    ASSERT_OK_AND_ASSIGN(auto valid_jwk, GenerateValidJwkString());

    std::vector<std::string> keys = {valid_jwk};
    auto status = ProcessCustomJwtKeys(keys, temp_dir);
    EXPECT_TRUE(status.ok()) << status.message();

    fs::path expected_file = temp_dir / "custom_key_0.jwk";
    EXPECT_TRUE(fs::exists(expected_file));

    auto file_content = android::base::file::read_whole_file(expected_file, false);
    ASSERT_TRUE(file_content.ok());
    EXPECT_FALSE(file_content->empty());
}

TEST(GrpcCustomKeyTest, InvalidKey_ReturnsError) {
    TestTempDir test_dir("jwk_invalid_test");
    fs::path temp_dir = test_dir.path;

    std::vector<std::string> keys = {"completely_invalid_key_string_without_colon"};
    auto status = ProcessCustomJwtKeys(keys, temp_dir);
    EXPECT_FALSE(status.ok());
}

TEST(GrpcCustomKeyTest, MissingJwkDir_ReturnsFailedPrecondition) {
    fs::path non_existent_dir = "/tmp/non_existent_jwk_dir_path_xyz_12345";
    ASSERT_OK_AND_ASSIGN(auto valid_jwk, GenerateValidJwkString());
    auto status = ProcessCustomJwtKeys({valid_jwk}, non_existent_dir);
    EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(GrpcCustomKeyTest, ConfigureJwtDirectory_WithCustomKeyOnly_ReturnsActiveJwkAndAuthenticates) {
    TestTempDir test_dir("jwk_custom_only_test");
    fs::path temp_dir = test_dir.path;

    ASSERT_OK_AND_ASSIGN(auto test_key, GenerateTestKey());

    // Simulate -device grpc,jwt_public_key=<jwk> without jwt=true.
    auto jwk_file = ConfigureJwtDirectory(temp_dir, {test_key.public_jwk_set});
    ASSERT_THAT(jwk_file, IsOk());
    EXPECT_EQ(*jwk_file, temp_dir / "active.jwk");

    fs::path custom_key_file = temp_dir / "custom_key_0.jwk";
    EXPECT_TRUE(android::base::file::exists(custom_key_file));

    auto allow_list = AllowList::FromJson(R"#({
        "allowlist": [
            {
                "iss": "custom-client",
                "allowed": [
                    "/android.emulation.control.EmulatorController/.*"
                ]
            }
        ]
    })#");

    JwtTokenAuth auth(jwk_file->parent_path(), *jwk_file, allow_list.get());
    EXPECT_TRUE(android::base::file::exists(*jwk_file));

    // Verify custom_key_0.jwk was not treated as jwks_loaded_path_ or overwritten with {"keys":[]}.
    auto custom_key_content = android::base::file::read_whole_file(custom_key_file, false);
    ASSERT_THAT(custom_key_content, IsOk());
    EXPECT_THAT(*custom_key_content, ::testing::Not(::testing::HasSubstr(R"({"keys": []})")));

    auto signer = test_key.private_handle->GetPrimitive<tink::JwtPublicKeySign>();
    ASSERT_THAT(signer, IsOk());
    const absl::Time now = absl::Now();
    auto raw_jwt = tink::RawJwtBuilder()
                           .SetIssuer("custom-client")
                           .SetIssuedAt(now)
                           .SetExpiration(now + absl::Minutes(5))
                           .Build();
    ASSERT_THAT(raw_jwt, IsOk());
    auto token = (*signer)->SignAndEncode(*raw_jwt);
    ASSERT_THAT(token, IsOk());

    EXPECT_THAT(auth.IsTokenValid("/android.emulation.control.EmulatorController/getStatus",
                                  absl::StrCat("Bearer ", *token)),
                IsOk());
}

TEST(GrpcCustomKeyTest, ConfigureJwtDirectory_UnicodeSubdir_WritesCustomKeyAndReturnsActiveJwk) {
    TestTempDir test_dir("jwk_unicode_test");
    fs::path unicode_dir = test_dir.path / "测试_🚀_é";
    ASSERT_THAT(android::base::file::mkdir_recursive(unicode_dir, 0700), IsOk());

    ASSERT_OK_AND_ASSIGN(auto valid_jwk, GenerateValidJwkString());
    auto jwk_file = ConfigureJwtDirectory(unicode_dir, {valid_jwk});
    ASSERT_THAT(jwk_file, IsOk());
    EXPECT_EQ(*jwk_file, unicode_dir / "active.jwk");
    EXPECT_TRUE(android::base::file::exists(unicode_dir / "custom_key_0.jwk"));
}

}  // namespace
}  // namespace goldfish::grpc
