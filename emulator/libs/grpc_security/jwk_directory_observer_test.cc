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
#include "android/emulation/control/jwk_directory_observer.h"

#include <gtest/gtest.h>
#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gtest/gtest_pred_impl.h"

#include "android/base/testing/test_event.h"
#include "android/base/testing/test_temp_dir.h"
#include "goldfish/file/file.h"
#include "tink/config/tink_config.h"
#include "tink/jwt/jwk_set_converter.h"
#include "tink/jwt/jwt_key_templates.h"
#include "tink/jwt/jwt_public_key_sign.h"
#include "tink/jwt/jwt_public_key_verify.h"
#include "tink/jwt/jwt_signature_config.h"
#include "tink/jwt/jwt_validator.h"
#include "tink/jwt/raw_jwt.h"
#include "tink/keyset_handle.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
namespace android {
namespace emulation {
namespace control {

namespace fs = std::filesystem;

using android::base::TestTempDir;
using json = nlohmann::json;
namespace tink = crypto::tink;
using crypto::tink::TinkConfig;

class JwkDirectoryObserverTest : public ::testing::Test {
  public:
    void SetUp() override {
        auto status = TinkConfig::Register();
        EXPECT_TRUE(status.ok());
        status = tink::JwtSignatureRegister();
        EXPECT_TRUE(status.ok());
        mTempDir = std::make_unique<TestTempDir>(
                absl::StrCat("watcher_test", TestTempDir::GenerateRandomString()));

        mSampleJwt = tink::RawJwtBuilder()
                             .SetIssuer("JwkDirectoryObserverTest")
                             .WithoutExpiration()
                             .Build();

        mSampleValidator = tink::JwtValidatorBuilder()
                                   .ExpectIssuer("JwkDirectoryObserverTest")
                                   .AllowMissingExpiration()
                                   .Build();
        mTestEv.Reset();
    }

    void TearDown() override {
        mTempDir.reset();
        mTestEv.Reset();
    }

    void write(fs::path fname, json snippet) { write(fname, snippet.dump(2)); }

    void write(fs::path fname, std::string snippet) {
        std::ofstream out(mTempDir->Path() / fname);
        out << snippet;
        out.close();
    }

    std::unique_ptr<KeysetHandle> writeEs512(fs::path fname) {
        // Let's generate a json key.
        auto status = tink::JwtSignatureRegister();
        EXPECT_TRUE(status.ok());
        auto private_handle = KeysetHandle::GenerateNew(tink::JwtEs512Template());
        EXPECT_TRUE(private_handle.ok());
        if (!private_handle.ok()) {
            return nullptr;
        }
        auto sign = (*private_handle)->GetPrimitive<tink::JwtPublicKeySign>();
        if (!sign.ok()) {
            return nullptr;
        }
        auto public_handle = (*private_handle)->GetPublicKeysetHandle();
        EXPECT_TRUE(public_handle.ok());
        if (!public_handle.ok()) {
            return nullptr;
        }
        auto jsonSnippet = tink::JwkSetFromPublicKeysetHandle(*public_handle->get());
        EXPECT_TRUE(jsonSnippet.ok());
        if (!jsonSnippet.ok()) {
            return nullptr;
        }
        write(fname, *jsonSnippet);
        return std::move(private_handle.value());
    }

  protected:
    std::unique_ptr<TestTempDir> mTempDir;
    TestEvent mTestEv;
    absl::StatusOr<tink::RawJwt> mSampleJwt;
    absl::StatusOr<tink::JwtValidator> mSampleValidator;
};

std::string RS256_snippet = R"(
{
   "keys":[
      {
         "kty":"RSA",
         "n":"AQAB",
         "e":"AQAB",
         "use":"sig",
         "alg":"RS256",
         "key_ops":[
            "verify"
         ],
         "kid":"ABC"
      }
   ]
})";

// ES256 snippet with
//
std::string ES256_snippet = R"(
{
   "keys":[
      {
         "crv":"P-256",
         "alg":"ES256",
         "kty":"EC",
         "x":"_EHh-Pq84GXDlrIbkcYUGAraDE7SLWvRZJZOgARK4II",
         "y":"vz3xxWFNDPyC-ePlmPF7cVqBsGqkQ2grL5K9ZzzrU2E",
         "kid":"DEF"
      }
   ]
})";

std::string ES256_PRIV = "9PPR4aq2V71P5QD_TJvPsM_edjcSOSkPEK1X3aasJHw";

TEST_F(JwkDirectoryObserverTest, no_jwks_results_in_event) {
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this](auto keyset) {
        // No keys found
        EXPECT_TRUE(keyset == nullptr);
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, finds_jwks) {
    write("sample.jwk", RS256_snippet);
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this](auto keyset) {
        EXPECT_EQ(keyset, nullptr);
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, duplicates_do_not_fail) {
    write("sample.jwk", RS256_snippet);
    write("sample2.jwk", RS256_snippet);
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this](auto keyset) {
        EXPECT_EQ(keyset, nullptr);
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, merging_multiple) {
    write("sample.jwk", RS256_snippet);
    write("sample2.jwk", ES256_snippet);
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this](auto keyset) {
        EXPECT_NE(keyset, nullptr);
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, create_and_validate) {
    auto private_handle = writeEs512("valid.jwk");
    auto sign = private_handle->GetPrimitive<tink::JwtPublicKeySign>();
    auto token = (*sign)->SignAndEncode(*mSampleJwt);
    ASSERT_THAT(token, absl_testing::IsOk());

    // Our observer found the public key, and hence can validate the token.
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this, token](auto keyset) {
        ASSERT_NE(keyset, nullptr);
        auto verify = keyset->template GetPrimitive<tink::JwtPublicKeyVerify>();
        ASSERT_THAT(verify, absl_testing::IsOk());
        auto validator = tink::JwtValidatorBuilder()
                                 .ExpectIssuer("JwkDirectoryObserverTest")
                                 .AllowMissingExpiration()
                                 .Build();
        ASSERT_THAT(validator, absl_testing::IsOk());
        auto verified_jwt = (*verify)->VerifyAndDecode(*token, *validator);
        ASSERT_THAT(verified_jwt, absl_testing::IsOk());
        EXPECT_EQ(*verified_jwt->GetIssuer(), "JwkDirectoryObserverTest");
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, create_multi_and_validate) {
    // Let's generate a series of json keys
    writeEs512("valid1.jwk");
    writeEs512("valid2.jwk");
    auto private_handle = writeEs512("valid.jwk");
    auto sign = private_handle->GetPrimitive<tink::JwtPublicKeySign>();
    auto token = (*sign)->SignAndEncode(*mSampleJwt);

    // Our observer found the public key, and hence can validate the token.
    JwkDirectoryObserver observer(mTempDir->Path().string(), [this, token](auto keyset) {
        ASSERT_NE(keyset, nullptr);
        auto verify = keyset->template GetPrimitive<tink::JwtPublicKeyVerify>();
        ASSERT_THAT(verify, absl_testing::IsOk());
        auto verified_jwt = (*verify)->VerifyAndDecode(*token, *mSampleValidator);
        ASSERT_THAT(verified_jwt, absl_testing::IsOk());
        EXPECT_EQ(*verified_jwt->GetIssuer(), "JwkDirectoryObserverTest");
        mTestEv.Signal();
    });
    mTestEv.Wait();
}

TEST_F(JwkDirectoryObserverTest, create_validate_and_delete) {
#ifdef __APPLE__
    GTEST_SKIP() << "This test is flaky on the build bots: b/233946633";
#endif
    // Let's generate a series of json keys
    writeEs512("valid1.jwk");
    writeEs512("valid2.jwk");
    auto private_handle = writeEs512("valid.jwk");
    auto sign = private_handle->GetPrimitive<tink::JwtPublicKeySign>();
    auto token = (*sign)->SignAndEncode(*mSampleJwt);

    enum TokenState { VALID_JWK_EXISTS, VALID_JWK_DELETED };
    std::atomic<TokenState> state = VALID_JWK_EXISTS;
    // Our observer found the public key, and hence can validate the token.
    JwkDirectoryObserver observer(mTempDir->Path().string(), [&](auto keyset) {
        ASSERT_NE(keyset, nullptr);
        auto verify = keyset->template GetPrimitive<tink::JwtPublicKeyVerify>();
        ASSERT_THAT(verify, absl_testing::IsOk());
        auto verified_jwt = (*verify)->VerifyAndDecode(*token, *mSampleValidator);
        switch (state) {
        case VALID_JWK_EXISTS:
            ASSERT_THAT(verified_jwt, absl_testing::IsOk());
            EXPECT_EQ(*verified_jwt->GetIssuer(), "JwkDirectoryObserverTest");
            mTestEv.Signal();
            break;
        case VALID_JWK_DELETED:
            EXPECT_FALSE(verified_jwt.ok());
            mTestEv.Signal();
        }
    });

    mTestEv.Wait();
    mTestEv.Reset();

    // We now are going to delete the valid jwk that was used to sign the
    // jwt. This means we no longer have the KID to validate the token in our
    // keyhandle. This in turns means that we will no longer be able to validate
    // the token.
    // Note, we might get multiple events.
    state = VALID_JWK_DELETED;
    auto todelete = mTempDir->Path() / "valid.jwk";
    base::file::rm(todelete).IgnoreError();
    mTestEv.Wait();
}

}  // namespace control
}  // namespace emulation
}  // namespace android
