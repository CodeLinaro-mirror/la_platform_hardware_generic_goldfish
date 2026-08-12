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
#include <string>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/test_temp_dir.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/file/file.h"
#include "tink/jwt/jwk_set_converter.h"
#include "tink/jwt/jwt_key_templates.h"
#include "tink/jwt/jwt_signature_config.h"
#include "tink/keyset_handle.h"

namespace fs = std::filesystem;
namespace tink = crypto::tink;
using android::base::TestTempDir;

namespace goldfish::grpc {

namespace {

absl::StatusOr<std::string> GenerateValidJwkString() {
    auto status = tink::JwtSignatureRegister();
    if (!status.ok()) return status;

    auto private_handle = tink::KeysetHandle::GenerateNew(tink::JwtEs512Template());
    if (!private_handle.ok()) return private_handle.status();

    auto public_handle = (*private_handle)->GetPublicKeysetHandle();
    if (!public_handle.ok()) return public_handle.status();

    return tink::JwkSetFromPublicKeysetHandle(*public_handle->get());
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

}  // namespace
}  // namespace goldfish::grpc
