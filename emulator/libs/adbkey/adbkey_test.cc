// Copyright 2020 The Android Open Source Project
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

#include "goldfish/adb/adbkey.h"

#include <gtest/gtest.h>
#include <openssl/obj_mac.h>
#include <openssl/rsa.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/match.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/process/process.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/adb/testing/adbkey_test_support.h"
#include "goldfish/file/file.h"

namespace goldfish::adb {

namespace {

namespace fs = std::filesystem;

using goldfish::adb::testing::CreateStubAdbExecutable;
using goldfish::adb::testing::GenerateTestAdbPrivateKey;
using goldfish::adb::testing::ScopedFakeAdbKeygen;
using goldfish::adb::testing::ScopedTestProcessFactory;

}  // namespace

class AdbKeyTest : public ::testing::Test {
  public:
    AdbKeyTest() {
        test_dir_ = test_system_.GetTempRoot();
        test_dir_->MakeSubDir(".android");
        test_system_.SetHomeDirectory(test_dir_->Path());
    }

    ~AdbKeyTest() override = default;

  protected:
    android::base::TestSystem test_system_{"/"};
    android::base::TestTempDir* test_dir_;
};

TEST_F(AdbKeyTest, KeyDoesNotExist) {
    EXPECT_EQ(fs::path{}, GetPrivateAdbKeyPath(test_dir_->Path() / ".android"));
}

TEST_F(AdbKeyTest, EmptyUserDirReturnsEmpty) {
    EXPECT_EQ(fs::path{}, GetPrivateAdbKeyPath(fs::path{}));
    auto res = EnsurePrivateAdbKey(fs::path{});
    EXPECT_THAT(res, absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(AdbKeyTest, EmptyHomeDirDoesNotFallbackToTempDir) {
    test_system_.SetHomeDirectory("");
    const fs::path temp_android = test_system_.GetTempDir() / ".android";
    ASSERT_OK(android::base::file::mkdir_recursive(temp_android, 0750));
    ASSERT_TRUE(GenerateTestAdbPrivateKey(temp_android / "adbkey"));

    const fs::path custom_dir = test_dir_->Path() / "custom_user_dir";
    EXPECT_EQ(fs::path{}, GetPrivateAdbKeyPath(custom_dir));
}

TEST_F(AdbKeyTest, FindKeysInDefaultPath) {
    test_dir_->MakeSubFile(".android/adbkey");
    EXPECT_EQ(test_dir_->Path() / ".android" / "adbkey",
              GetPrivateAdbKeyPath(test_dir_->Path() / ".android"));
}

TEST_F(AdbKeyTest, CopiesDefaultKeyToCustomUserDir) {
    const fs::path default_priv = test_dir_->Path() / ".android" / "adbkey";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(default_priv));
    ASSERT_OK_AND_ASSIGN(const std::string expected_priv,
                         android::base::file::read_whole_file(default_priv, /*binary=*/true));

    const fs::path custom_dir = test_dir_->Path() / "custom_android_user";
    EXPECT_EQ(custom_dir / "adbkey", GetPrivateAdbKeyPath(custom_dir));

    ASSERT_OK_AND_ASSIGN(const std::string copied_priv,
                         android::base::file::read_whole_file(custom_dir / "adbkey",
                                                              /*binary=*/true));
    EXPECT_EQ(copied_priv, expected_priv);

#ifndef _WIN32
    ASSERT_OK_AND_ASSIGN(const unsigned priv_mode,
                         android::base::file::mode(custom_dir / "adbkey"));
    EXPECT_EQ(priv_mode & 0777U, 0600U);
#endif
}

TEST_F(AdbKeyTest, GenerateWritesAKey) {
    const fs::path key_file = test_dir_->Path() / ".android" / "adbsamplekey";
    EXPECT_FALSE(android::base::file::exists(key_file));
    EXPECT_TRUE(GenerateTestAdbPrivateKey(key_file));
    EXPECT_TRUE(android::base::file::exists(key_file));
#ifndef _WIN32
    ASSERT_OK_AND_ASSIGN(const unsigned key_mode, android::base::file::mode(key_file));
    EXPECT_EQ(key_mode & 0777U, 0600U);
#endif
}

TEST_F(AdbKeyTest, CanCreatePubFromGeneratedPriv) {
    std::string pubkey;
    const fs::path key_file = test_dir_->Path() / ".android" / "adbsamplekey2";
    EXPECT_TRUE(GenerateTestAdbPrivateKey(key_file));
    EXPECT_TRUE(PubkeyFromPrivkey(key_file, &pubkey));
    EXPECT_FALSE(pubkey.empty());
}

TEST_F(AdbKeyTest, PubkeyFromPrivkeyUsesTestSystemEnvAndFallback) {
    const fs::path key_file = test_dir_->Path() / ".android" / "sample_priv";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(key_file));

    test_system_.EnvSet("LOGNAME", "");
    test_system_.EnvSet("USER", "");
    test_system_.EnvSet("USERNAME", "");
    test_system_.EnvSet("HOSTNAME", "");
    test_system_.EnvSet("COMPUTERNAME", "");

    std::string fallback_pubkey;
    ASSERT_TRUE(PubkeyFromPrivkey(key_file, &fallback_pubkey));
    EXPECT_TRUE(absl::EndsWith(fallback_pubkey, " unknown@unknown")) << fallback_pubkey;

    test_system_.EnvSet("LOGNAME", "alice");
    test_system_.EnvSet("HOSTNAME", "workstation");
    std::string env_pubkey;
    ASSERT_TRUE(PubkeyFromPrivkey(key_file, &env_pubkey));
    EXPECT_TRUE(absl::EndsWith(env_pubkey, " alice@workstation")) << env_pubkey;
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyReturnsExistingWithoutRunningAdb) {
    const fs::path key_file = test_dir_->Path() / ".android" / "adbkey";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(key_file));

    ScopedFakeAdbKeygen fake_keygen;
    ASSERT_OK_AND_ASSIGN(const AdbKeyResult res,
                         EnsurePrivateAdbKey(test_dir_->Path() / ".android"));
    EXPECT_FALSE(res.newly_created);
    EXPECT_EQ(res.private_key_path, key_file);
    EXPECT_EQ(fake_keygen.invocations(), 0);
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyGeneratesViaSdkPlatformTools) {
    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    const fs::path adb_exe = CreateStubAdbExecutable(sdk_dir / "platform-tools");
    const fs::path user_dir = test_dir_->Path() / ".android";

    ScopedFakeAdbKeygen fake_keygen;
    ASSERT_OK_AND_ASSIGN(const AdbKeyResult first, EnsurePrivateAdbKey(user_dir, sdk_dir));
    EXPECT_TRUE(first.newly_created);
    EXPECT_EQ(first.private_key_path, user_dir / "adbkey");
    EXPECT_EQ(fake_keygen.invocations(), 1);

    const android::base::CommandArguments captured_args = fake_keygen.last_args();
    ASSERT_EQ(captured_args.size(), 3U);
    EXPECT_EQ(fs::path(captured_args[0]), adb_exe);
    EXPECT_EQ(captured_args[1], "keygen");
    EXPECT_NE(fs::path(captured_args[2]), user_dir / "adbkey");

    EXPECT_TRUE(android::base::file::is_file(user_dir / "adbkey"));
    EXPECT_TRUE(android::base::file::is_file(user_dir / "adbkey.pub"));
    for (const fs::path& entry : android::base::file::scan_dir(user_dir)) {
        EXPECT_FALSE(absl::StrContains(entry.string(), ".tmp.")) << entry;
    }

    std::string derived_pub;
    ASSERT_TRUE(PubkeyFromPrivkey(first.private_key_path, &derived_pub));
    EXPECT_FALSE(derived_pub.empty());

    // Subsequent calls see the existing key and do not invoke `adb keygen` again.
    ASSERT_OK_AND_ASSIGN(const AdbKeyResult second, EnsurePrivateAdbKey(user_dir, sdk_dir));
    EXPECT_FALSE(second.newly_created);
    EXPECT_EQ(second.private_key_path, user_dir / "adbkey");
    EXPECT_EQ(fake_keygen.invocations(), 1);
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyFindsAdbOnPathAndSupportsNonAsciiDirectory) {
    const fs::path bin_dir = test_dir_->Path() / "工具_bin";
    const fs::path adb_exe = CreateStubAdbExecutable(bin_dir);
    test_system_.EnvSet("PATH", "\"" + bin_dir.string() + "\"");

    const fs::path non_ascii_user_dir = test_dir_->Path() / "测试_🚀_é" / ".android";

    ScopedFakeAdbKeygen fake_keygen;
    ASSERT_OK_AND_ASSIGN(const AdbKeyResult res, EnsurePrivateAdbKey(non_ascii_user_dir));
    EXPECT_TRUE(res.newly_created);
    EXPECT_EQ(res.private_key_path, non_ascii_user_dir / "adbkey");
    EXPECT_TRUE(android::base::file::is_file(adb_exe));
    EXPECT_EQ(fake_keygen.invocations(), 1);

    std::string pubkey;
    ASSERT_TRUE(PubkeyFromPrivkey(res.private_key_path, &pubkey));
    EXPECT_FALSE(pubkey.empty());
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyFailsWhenAdbNotFound) {
    test_system_.EnvSet("PATH", "");

    const auto no_sdk_res = EnsurePrivateAdbKey(test_dir_->Path() / ".android");
    EXPECT_THAT(
            no_sdk_res,
            absl_testing::StatusIs(
                    absl::StatusCode::kNotFound,
                    ::testing::AllOf(::testing::HasSubstr("(no SDK directory)"),
                                     ::testing::HasSubstr("install Android SDK Platform-Tools"))));

    const fs::path sdk_dir = test_dir_->Path() / "missing_sdk";
    const auto sdk_res = EnsurePrivateAdbKey(test_dir_->Path() / ".android", sdk_dir);
    EXPECT_THAT(
            sdk_res,
            absl_testing::StatusIs(
                    absl::StatusCode::kNotFound,
                    ::testing::AllOf(::testing::HasSubstr((sdk_dir / "platform-tools").string()),
                                     ::testing::HasSubstr("install Android SDK Platform-Tools"))));
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyReplacesOrphanedStalePublicKey) {
    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    const fs::path user_dir = test_dir_->Path() / ".android";
    const fs::path pubkey_path = user_dir / "adbkey.pub";
    {
        std::ofstream out(pubkey_path, std::ios::binary | std::ios::trunc);
        out << "stale-orphaned-public-key";
    }

    ScopedFakeAdbKeygen fake_keygen;
    ASSERT_OK_AND_ASSIGN(const AdbKeyResult res, EnsurePrivateAdbKey(user_dir, sdk_dir));
    EXPECT_TRUE(res.newly_created);
    EXPECT_EQ(fake_keygen.invocations(), 1);

    std::string expected_pub;
    ASSERT_TRUE(PubkeyFromPrivkey(res.private_key_path, &expected_pub));
    ASSERT_OK_AND_ASSIGN(const std::string actual_pub,
                         android::base::file::read_whole_file(pubkey_path));
    EXPECT_NE(actual_pub, "stale-orphaned-public-key");
    EXPECT_THAT(actual_pub, ::testing::StartsWith(expected_pub));
}

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyFailsWhenExistingKeyIsNotReadableFile) {
    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    const fs::path user_dir = test_dir_->Path() / ".android";
    ASSERT_OK(android::base::file::mkdir_recursive(user_dir / "adbkey", 0750));

    ScopedFakeAdbKeygen fake_keygen;
    const auto res = EnsurePrivateAdbKey(user_dir, sdk_dir);
    EXPECT_THAT(res, absl_testing::StatusIs(absl::StatusCode::kPermissionDenied));
    EXPECT_EQ(fake_keygen.invocations(), 0);
}

#ifndef _WIN32
TEST_F(AdbKeyTest, UnreadableExistingKeyIsNotOverwritten) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "Root bypasses mode 0000 read checks";
    }

    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    CreateStubAdbExecutable(sdk_dir / "platform-tools");

    const fs::path default_priv = test_dir_->Path() / ".android" / "adbkey";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(default_priv));

    const fs::path custom_dir = test_dir_->Path() / "custom_user";
    ASSERT_OK(android::base::file::mkdir_recursive(custom_dir, 0750));
    const fs::path unreadable_priv = custom_dir / "adbkey";
    ASSERT_TRUE(GenerateTestAdbPrivateKey(unreadable_priv));
    ASSERT_OK_AND_ASSIGN(const std::string original_bytes,
                         android::base::file::read_whole_file(unreadable_priv, /*binary=*/true));

    ASSERT_OK(android::base::file::chmod(unreadable_priv, 0000));
    absl::Cleanup restore_perms = [&unreadable_priv]() {
        android::base::file::chmod(unreadable_priv, 0600).IgnoreError();
    };

    EXPECT_EQ(fs::path{}, GetPrivateAdbKeyPath(custom_dir));

    ScopedFakeAdbKeygen fake_keygen;
    const auto res = EnsurePrivateAdbKey(custom_dir, sdk_dir);
    EXPECT_THAT(res, absl_testing::StatusIs(absl::StatusCode::kPermissionDenied,
                                            ::testing::HasSubstr("exists but is not readable")));
    EXPECT_EQ(fake_keygen.invocations(), 0);

    ASSERT_OK(android::base::file::chmod(unreadable_priv, 0600));
    ASSERT_OK_AND_ASSIGN(const std::string preserved_bytes,
                         android::base::file::read_whole_file(unreadable_priv, /*binary=*/true));
    EXPECT_EQ(preserved_bytes, original_bytes);
}
#endif

TEST_F(AdbKeyTest, EnsurePrivateAdbKeyFailsWhenAdbKeygenExitsNonZero) {
    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    const fs::path user_dir = test_dir_->Path() / ".android";

    ScopedTestProcessFactory factory([](const android::base::CommandArguments& args,
                                        std::string* /*out*/,
                                        std::string* err) -> android::base::ProcessExitCode {
        if (args.size() == 3) {
            // Simulate partial temp file left behind before failure.
            std::ofstream(fs::path(args[2]), std::ios::trunc) << "partial";
        }
        *err = "adb keygen simulated failure";
        return 2;
    });

    auto res = EnsurePrivateAdbKey(user_dir, sdk_dir);
    EXPECT_THAT(res, absl_testing::StatusIs(absl::StatusCode::kInternal));
    EXPECT_FALSE(android::base::file::exists(user_dir / "adbkey"));
    for (const fs::path& entry : android::base::file::scan_dir(user_dir)) {
        EXPECT_FALSE(absl::StrContains(entry.string(), ".tmp.")) << entry;
    }
}

TEST_F(AdbKeyTest, ConcurrentEnsurePrivateAdbKeyGeneratesOnce) {
    const fs::path sdk_dir = test_dir_->Path() / "sdk";
    CreateStubAdbExecutable(sdk_dir / "platform-tools");
    const fs::path user_dir = test_dir_->Path() / ".android";

    ScopedFakeAdbKeygen fake_keygen(std::chrono::milliseconds(25));

    constexpr int kNumThreads = 4;
    std::atomic<int> ready_count{0};
    std::atomic<bool> start{false};
    std::vector<absl::StatusOr<AdbKeyResult>> results(kNumThreads);
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&, i]() {
            ready_count.fetch_add(1, std::memory_order_acq_rel);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            results[i] = EnsurePrivateAdbKey(user_dir, sdk_dir);
        });
    }

    while (ready_count.load(std::memory_order_acquire) < kNumThreads) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    int newly_created_count = 0;
    std::string expected_pubkey;
    for (int i = 0; i < kNumThreads; ++i) {
        ASSERT_OK(results[i].status());
        EXPECT_EQ(results[i]->private_key_path, user_dir / "adbkey");
        if (results[i]->newly_created) {
            ++newly_created_count;
        }
        std::string pubkey;
        ASSERT_TRUE(PubkeyFromPrivkey(results[i]->private_key_path, &pubkey));
        if (expected_pubkey.empty()) {
            expected_pubkey = pubkey;
        } else {
            EXPECT_EQ(pubkey, expected_pubkey);
        }
    }

    EXPECT_EQ(fake_keygen.invocations(), 1);
    EXPECT_EQ(newly_created_count, 1);
}

// Test digest to verify.
const uint8_t kDigest[] = {
    0x31, 0x5f, 0x5b, 0xdb, 0x76, 0xd0, 0x78, 0xc4, 0x3b, 0x8a, 0xc0, 0x06, 0x4e, 0x4a, 0x01, 0x64,
    0x61, 0x2b, 0x1f, 0xce, 0x77, 0xc8, 0x69, 0x34, 0x5b, 0xfc, 0x94, 0xc7, 0x58, 0x94, 0xed, 0xd3,
};

// 2048 RSA test key.
const uint8_t kKey2048[internal::kAndroidPubkeyEncodedSize] = {
    0x40, 0x00, 0x00, 0x00, 0x05, 0x75, 0x61, 0xd1, 0x33, 0xf0, 0x2d, 0x12, 0x45, 0xfb, 0xae, 0x07,
    0x02, 0x15, 0x4f, 0x3a, 0x2b, 0xa3, 0xbc, 0x49, 0xbd, 0x14, 0x07, 0xa0, 0xc0, 0x9f, 0x0c, 0x52,
    0x60, 0x77, 0x9f, 0xa2, 0x31, 0xd0, 0xa7, 0xfb, 0x7e, 0xde, 0xfb, 0xc9, 0x05, 0xc0, 0x97, 0xf7,
    0x74, 0x99, 0xe6, 0xd1, 0x08, 0xa6, 0xc2, 0x59, 0x5a, 0xd8, 0x37, 0x1d, 0xe0, 0x48, 0x5e, 0x63,
    0x44, 0x04, 0x8b, 0x05, 0x20, 0xf6, 0x25, 0x67, 0x38, 0xb2, 0xb6, 0xf9, 0xbe, 0xb6, 0x1d, 0x7f,
    0x1b, 0x71, 0x8a, 0xeb, 0xb7, 0xf8, 0x01, 0xc1, 0x5e, 0xf7, 0xfe, 0x48, 0x08, 0x27, 0x0f, 0x27,
    0x2a, 0x64, 0x1a, 0x43, 0x8d, 0xcf, 0x5a, 0x33, 0x5c, 0x18, 0xc5, 0xf4, 0xe7, 0xfe, 0xee, 0xd3,
    0x12, 0x62, 0xad, 0x61, 0x78, 0x9a, 0x03, 0xb0, 0xaf, 0xab, 0x91, 0x57, 0x46, 0xbf, 0x18, 0xc6,
    0xbc, 0x0c, 0x6b, 0x55, 0xcd, 0xda, 0xc4, 0xcc, 0x98, 0x46, 0x91, 0x99, 0xbc, 0xa3, 0xca, 0x6c,
    0x86, 0xa6, 0x1c, 0x8f, 0xca, 0xf8, 0xf6, 0x8a, 0x00, 0x8e, 0x05, 0xd7, 0x13, 0x43, 0xe2, 0xf2,
    0x1a, 0x13, 0xf3, 0x50, 0x13, 0xa4, 0xf2, 0x4e, 0x41, 0xb1, 0x36, 0x78, 0x55, 0x4c, 0x5e, 0x27,
    0xc5, 0xc0, 0x4b, 0xd8, 0x93, 0xaa, 0x7e, 0xf0, 0x90, 0x08, 0x10, 0x26, 0x72, 0x6d, 0xb9, 0x21,
    0xae, 0x4d, 0x01, 0x4b, 0x55, 0x1d, 0xe7, 0x1e, 0x5e, 0x31, 0x6e, 0x62, 0xd1, 0x33, 0x26, 0xcb,
    0xdb, 0xfe, 0x72, 0x98, 0xc8, 0x06, 0x1c, 0x12, 0xdf, 0xfc, 0x74, 0xe5, 0x7a, 0x6f, 0xf5, 0xa3,
    0x63, 0x08, 0xe3, 0x02, 0x68, 0x4d, 0x7c, 0x70, 0x05, 0xec, 0x95, 0x7e, 0x24, 0xa4, 0xbc, 0x4c,
    0xcd, 0x39, 0x14, 0xb5, 0x2a, 0x8f, 0xc1, 0xe3, 0x4e, 0xfa, 0xf8, 0x70, 0x50, 0x8f, 0xd5, 0x8e,
    0xc7, 0xb5, 0x32, 0x89, 0x4d, 0xbb, 0x6a, 0xc1, 0xc1, 0xa2, 0x42, 0x57, 0x57, 0xbd, 0x2a, 0xdc,
    0xa6, 0xfd, 0xc8, 0x86, 0x44, 0x6a, 0x03, 0x5d, 0x4d, 0x28, 0xe1, 0xde, 0xb4, 0xa9, 0xa5, 0x03,
    0x61, 0x7a, 0x5f, 0xb1, 0x09, 0x17, 0x2b, 0x9c, 0xa2, 0x54, 0x28, 0xad, 0x34, 0xc9, 0x5f, 0x6c,
    0x9f, 0xb8, 0xd2, 0xa9, 0x78, 0xa7, 0xaa, 0xb3, 0x11, 0x2f, 0x65, 0x9b, 0x4e, 0x67, 0x0c, 0xcc,
    0x20, 0x36, 0xbf, 0x26, 0x2b, 0x4e, 0xc0, 0xd4, 0xbd, 0x22, 0x64, 0xc4, 0x1c, 0x56, 0x69, 0xdb,
    0x5f, 0x89, 0xe1, 0x75, 0x68, 0x8d, 0x0e, 0xab, 0x1c, 0x10, 0x1a, 0xc0, 0x12, 0x5d, 0x6f, 0xbd,
    0x09, 0xbb, 0x47, 0xcb, 0xe7, 0x34, 0xef, 0x56, 0xab, 0xea, 0xc3, 0xe9, 0x7f, 0x9a, 0x3d, 0xe9,
    0x2d, 0x14, 0x61, 0x25, 0x37, 0x5c, 0x3b, 0x4b, 0xaf, 0x5a, 0x4b, 0xc8, 0x99, 0x1a, 0x32, 0x8f,
    0x54, 0x07, 0xd3, 0x57, 0x8a, 0x3d, 0x2a, 0xf7, 0x9e, 0x7e, 0x92, 0x2a, 0x50, 0xe9, 0xd8, 0xdb,
    0xd6, 0x03, 0xd3, 0x8e, 0x54, 0x32, 0xce, 0x87, 0x93, 0x92, 0xe7, 0x75, 0xe1, 0x6b, 0x78, 0x1a,
    0x85, 0xc2, 0x46, 0xa1, 0x31, 0xbb, 0xc7, 0xb9, 0x1d, 0xd1, 0x71, 0xe0, 0xe2, 0x9b, 0x9c, 0x0d,
    0xa3, 0xcf, 0x93, 0x4d, 0x87, 0x7b, 0x65, 0xd9, 0xda, 0x4c, 0xd9, 0x6a, 0xa6, 0x36, 0xc2, 0xc7,
    0xe3, 0x33, 0xe2, 0xc3, 0x83, 0xd1, 0x72, 0x54, 0x30, 0x81, 0x5e, 0x34, 0x2c, 0x61, 0xee, 0xf4,
    0x48, 0x97, 0xb6, 0xaa, 0x47, 0x6a, 0x05, 0x09, 0xd8, 0x4d, 0x90, 0xaf, 0xa8, 0x4e, 0x82, 0xe4,
    0x8e, 0xb5, 0xe2, 0x65, 0x86, 0x67, 0xe9, 0x5b, 0x4b, 0x9a, 0x68, 0x08, 0x30, 0xf6, 0x25, 0x8b,
    0x20, 0xda, 0x26, 0x6f, 0xbd, 0x0d, 0xa5, 0xd8, 0x6a, 0x7b, 0x01, 0x2f, 0xab, 0x7b, 0xb5, 0xfe,
    0x62, 0x37, 0x2d, 0x94, 0x43, 0x2f, 0x4d, 0x16, 0x01, 0x00, 0x01, 0x00,
};

// 2048 bit RSA signature.
const uint8_t kSignature2048[internal::kAndroidPubkeyModulusSize] = {
    0x3a, 0x11, 0x84, 0x40, 0xc1, 0x2f, 0x13, 0x8c, 0xde, 0xb0, 0xc3, 0x89, 0x8a, 0x63, 0xb2, 0x50,
    0x93, 0x58, 0xc0, 0x0c, 0xb7, 0x08, 0xe7, 0x6c, 0x52, 0x87, 0x4e, 0x78, 0x89, 0xa3, 0x9a, 0x47,
    0xeb, 0x11, 0x57, 0xbc, 0xb3, 0x97, 0xf8, 0x34, 0xf1, 0xf7, 0xbf, 0x3a, 0xfa, 0x1c, 0x6b, 0xdc,
    0xd1, 0x02, 0xde, 0x9a, 0x0d, 0x72, 0xe7, 0x19, 0x63, 0x81, 0x46, 0x68, 0x1e, 0x63, 0x64, 0xc6,
    0x59, 0xe7, 0x7c, 0x39, 0xed, 0x32, 0xd2, 0xd1, 0xd5, 0x1f, 0x13, 0x9b, 0x52, 0xdf, 0x34, 0xa3,
    0xc0, 0xc4, 0x9a, 0x63, 0x9b, 0x9c, 0xbe, 0x22, 0xc8, 0xd8, 0x14, 0x2f, 0x4c, 0x78, 0x36, 0xdb,
    0x16, 0x41, 0x67, 0xc1, 0x21, 0x8a, 0x73, 0xb2, 0xe5, 0xb0, 0xd3, 0x80, 0x91, 0x7a, 0xbf, 0xf9,
    0x59, 0x4a, 0x4d, 0x78, 0x45, 0x44, 0xa1, 0x52, 0x86, 0x29, 0x48, 0x4d, 0xf0, 0x5d, 0xf2, 0x55,
    0xa7, 0xcd, 0xc5, 0x2b, 0x7b, 0xe0, 0xb1, 0xf6, 0x2a, 0xd5, 0x61, 0xba, 0x1e, 0x1e, 0x3a, 0xf0,
    0x55, 0xbc, 0x8c, 0x44, 0x41, 0xfc, 0xb8, 0x8c, 0x76, 0xbf, 0x80, 0x58, 0x82, 0x35, 0x4b, 0x0c,
    0xfd, 0xef, 0xd5, 0x70, 0xd1, 0x64, 0xcb, 0x46, 0x58, 0x37, 0xbc, 0xa9, 0x7d, 0xd4, 0x70, 0xac,
    0xce, 0xec, 0xca, 0x48, 0xcb, 0x0a, 0x40, 0x77, 0x04, 0x59, 0xca, 0x9c, 0x7d, 0x1a, 0x0b, 0xf0,
    0xb5, 0xdd, 0xde, 0x71, 0x18, 0xb8, 0xef, 0x90, 0x2a, 0x09, 0x42, 0x39, 0x74, 0xff, 0x45, 0xa1,
    0x39, 0x17, 0x50, 0x89, 0xa6, 0x5f, 0xbc, 0x9c, 0x0c, 0x9b, 0x47, 0x25, 0x79, 0x3e, 0xe3, 0xaa,
    0xaf, 0xbe, 0x73, 0x6b, 0xcb, 0xe7, 0x35, 0xc1, 0x27, 0x09, 0xcd, 0xeb, 0xd7, 0xcf, 0x63, 0x83,
    0x64, 0x8c, 0x45, 0x1c, 0x1d, 0x58, 0xcc, 0xd2, 0xf8, 0x2b, 0x4c, 0x4e, 0x14, 0x89, 0x2d, 0x70,
};

struct AndroidPubkeyTest : public ::testing::Test {
    void SetUp() override {
        RSA* new_key = nullptr;
        internal::AndroidPubkeyDecode(kKey2048, sizeof(kKey2048), &new_key);
        key.reset(new_key);
    }

    std::unique_ptr<RSA, void (*)(RSA*)> key = {nullptr, RSA_free};
};

TEST_F(AndroidPubkeyTest, Decode) {
    // Make sure the decoded key successfully verifies a valid signature.
    EXPECT_TRUE(RSA_verify(NID_sha256, kDigest, sizeof(kDigest), kSignature2048,
                           sizeof(kSignature2048), key.get()));
}

TEST_F(AndroidPubkeyTest, Encode) {
    uint8_t key_data[internal::kAndroidPubkeyEncodedSize];
    ASSERT_TRUE(internal::AndroidPubkeyEncode(key.get(), key_data, sizeof(key_data)));
    ASSERT_EQ(0, memcmp(kKey2048, key_data, sizeof(kKey2048)));
}

}  // namespace goldfish::adb
