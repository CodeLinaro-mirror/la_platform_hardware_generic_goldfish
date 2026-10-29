#include "telnet_auth.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <aclapi.h>
#include <windows.h>
#endif

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"

namespace goldfish::telnet {

namespace {

using ::testing::HasSubstr;
using ::testing::Not;

}  // namespace

class TelnetAuthTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a temporary home directory for testing
        test_home_ = tmpdir_.Path() / "test_home_auth";
        auto status = android::base::file::mkdir_recursive(test_home_, 0700);
        ASSERT_TRUE(status.ok()) << "Failed to create test home directory: " << status.message();
        test_system_.SetHomeDirectory(test_home_);
        token_path_ = test_home_ / ".emulator_console_auth_token";
    }

    void TearDown() override { std::filesystem::remove_all(test_home_); }

    std::filesystem::path test_home_;
    std::filesystem::path token_path_;
    android::base::TestSystem test_system_{"/foo/bar"};
    android::base::TestTempDir tmpdir_{"TelnetAuthTest"};
};

// --- Section 1: Read-only paths (No Side Effects) ---

TEST_F(TelnetAuthTest, ReadToken_ReturnsNotFound_WhenFileMissing) {
    const absl::StatusOr<TelnetAuth::Token> result = TelnetAuth::ReadToken(token_path_);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsNotFound(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr(token_path_.string()));
    EXPECT_THAT(result.status().message(), HasSubstr("file does not exist"));
    EXPECT_THAT(result.status().message(), HasSubstr("Verify the token path"));

    // Side effect check: File must NOT have been created
    EXPECT_FALSE(android::base::file::exists(token_path_));
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsRequired_WhenFileMissing) {
    // If the file is missing, we still return kRequired (meaning authentication
    // will be needed once the file is provisioned).
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kRequired);

    // Side effect check: File must NOT have been created
    EXPECT_FALSE(std::filesystem::exists(token_path_));
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsDisabled_WhenFileEmpty) {
    {
        std::ofstream ofs(token_path_);
        // Create empty file
    }
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kDisabled);
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsError_WhenFileTooLarge) {
    {
        std::ofstream ofs(token_path_);
        // 1KB limit is 1024 bytes. Let's make it 1025.
        std::string large_content(1025, 'A');
        ofs << large_content;
    }
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kError);
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsRequired_WhenFileIsExactly1KB) {
    {
        std::ofstream ofs(token_path_);
        // 1KB limit is 1024 bytes.
        std::string content(1024, 'A');
        ofs << content;
    }
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kRequired);
}

TEST_F(TelnetAuthTest, ReadToken_ReturnsError_WhenFileTooLarge) {
    constexpr std::string_view kSecretMarker = "SECRET_MARKER_";
    const std::string large_content =
            std::string(kSecretMarker) + std::string(1025 - kSecretMarker.size(), 'A');
    ASSERT_TRUE(android::base::file::CreatePrivateFileExclusive(token_path_, large_content).ok());

    const absl::StatusOr<TelnetAuth::Token> result = TelnetAuth::ReadToken(token_path_);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsResourceExhausted(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr(token_path_.string()));
    EXPECT_THAT(result.status().message(), HasSubstr("1025 bytes"));
    EXPECT_THAT(result.status().message(), HasSubstr("1024 bytes"));
    EXPECT_THAT(result.status().message(), Not(HasSubstr(kSecretMarker)));
}

TEST_F(TelnetAuthTest, ReadToken_ReturnsFailedPrecondition_WhenPathIsDirectory) {
    ASSERT_TRUE(android::base::file::mkdir(token_path_, 0700).ok());

    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kError);

    const absl::StatusOr<TelnetAuth::Token> result = TelnetAuth::ReadToken(token_path_);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsFailedPrecondition(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr(token_path_.string()));
    EXPECT_THAT(result.status().message(), HasSubstr("not a regular file"));
}

TEST_F(TelnetAuthTest, ReadToken_Succeeds_WhenFileIsExactly1KB) {
    std::string content(1024, 'A');
    {
        std::ofstream ofs(token_path_);
        ofs << content;
    }
    auto result = TelnetAuth::ReadToken(token_path_);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->AsStringView().size(), 1024);
    EXPECT_EQ(result->AsStringView(), content);
}

#ifndef _WIN32
TEST_F(TelnetAuthTest, GetStatus_ReturnsError_WhenFileUnreadable) {
    {
        std::ofstream ofs(token_path_);
        ofs << "some-token";
    }
    // Make it unreadable
    std::filesystem::permissions(token_path_, std::filesystem::perms::none);

    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kError);

    // Restore permissions so TearDown can delete it cleanly
    std::filesystem::permissions(token_path_, std::filesystem::perms::owner_all);
}

TEST_F(TelnetAuthTest, ReadToken_ReturnsError_WhenFileUnreadable) {
    constexpr std::string_view kSecretToken = "SECRET_UNREADABLE_TOKEN";
    ASSERT_TRUE(android::base::file::CreatePrivateFileExclusive(token_path_, kSecretToken).ok());
    ASSERT_TRUE(android::base::file::chmod(token_path_, 0000).ok());

    const absl::StatusOr<TelnetAuth::Token> result = TelnetAuth::ReadToken(token_path_);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsPermissionDenied(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr(token_path_.string()));
    EXPECT_THAT(result.status().message(), HasSubstr("insufficient permissions"));
    EXPECT_THAT(result.status().message(), HasSubstr("Grant read permissions"));
    EXPECT_THAT(result.status().message(), Not(HasSubstr(kSecretToken)));

    // Restore permissions so TearDown can delete it cleanly
    EXPECT_TRUE(android::base::file::chmod(token_path_, 0700).ok());
}
#endif

// --- Section 2: Existence & Provisioning ---

TEST_F(TelnetAuthTest, LoadOrCreate_ReturnsExistingToken) {
    std::string secret = "pre-existing-token";
    {
        std::ofstream ofs(token_path_);
        ofs << secret;
    }

    auto result = TelnetAuth::LoadOrCreateToken(16, token_path_);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->AsStringView(), secret);
}

TEST_F(TelnetAuthTest, ReadToken_TrimsWhitespace) {
    std::string secret = "whitespace-token";
    {
        std::ofstream ofs(token_path_);
        ofs << "  " << secret << "\n\r\n ";
    }

    auto result = TelnetAuth::ReadToken(token_path_);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->AsStringView(), secret);
}

TEST_F(TelnetAuthTest, Token_SecureEquals) {
    TelnetAuth::Token token("my-secret-token");
    EXPECT_TRUE(token.SecureEquals("my-secret-token"));
    EXPECT_FALSE(token.SecureEquals("wrong-token"));
    EXPECT_FALSE(token.SecureEquals("my-secret-toke"));
    EXPECT_FALSE(token.SecureEquals("my-secret-token-extra"));
}

TEST_F(TelnetAuthTest, LoadOrCreate_ProvisionsNewToken_AtCustomPath) {
    auto custom_token_path = test_home_ / "custom_token";
    ASSERT_FALSE(std::filesystem::exists(custom_token_path));
    ASSERT_FALSE(std::filesystem::exists(token_path_));

    auto result = TelnetAuth::LoadOrCreateToken(16, custom_token_path);
    ASSERT_TRUE(result.ok());

    // Verify file was created at custom path
    EXPECT_TRUE(std::filesystem::exists(custom_token_path));
    // Verify file was NOT created at default path
    EXPECT_FALSE(std::filesystem::exists(token_path_));

    // Verify file content matches
    auto read_back = TelnetAuth::ReadToken(custom_token_path);
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->AsStringView(), result->AsStringView());
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsDisabled_WhenCustomFileEmpty) {
    auto custom_token_path = test_home_ / "custom_token_empty";
    {
        std::ofstream ofs(custom_token_path);
        // Create empty file
    }
    EXPECT_EQ(TelnetAuth::GetStatus(custom_token_path), AuthStatus::kDisabled);
}

TEST_F(TelnetAuthTest, LoadOrCreate_ProvisionsNewToken_WhenMissing) {
    ASSERT_FALSE(std::filesystem::exists(token_path_));

    auto result = TelnetAuth::LoadOrCreateToken(16, token_path_);
    ASSERT_TRUE(result.ok());
    // WebSafeBase64 encoding of 16 bytes of entropy results in a 22-character string (no padding).
    EXPECT_EQ(result->AsStringView().length(), 22)
            << "Token should have 16 bytes of entropy (22 chars), got: [" << result->AsStringView()
            << "]";

    // Verify file was actually created and is readable
    auto read_back = TelnetAuth::ReadToken(token_path_);
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->AsStringView(), result->AsStringView());
}

#ifndef _WIN32
TEST_F(TelnetAuthTest, ProvisionsWithSecurePermissions) {
    auto result = TelnetAuth::LoadOrCreateToken(16, token_path_);
    ASSERT_TRUE(result.ok());

    // Verify permissions are 0600 (owner read/write only)
    auto perms = std::filesystem::status(token_path_).permissions();

    // Check that group and others have NO permissions.
    EXPECT_EQ(perms & std::filesystem::perms::group_all, std::filesystem::perms::none);
    EXPECT_EQ(perms & std::filesystem::perms::others_all, std::filesystem::perms::none);

    // Check that owner has at least read/write.
    EXPECT_EQ(perms & (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
              std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
}
#else
TEST_F(TelnetAuthTest, ProvisionsWithSecurePermissionsWindows) {
    auto result = TelnetAuth::LoadOrCreateToken(16, token_path_);
    ASSERT_TRUE(result.ok());

    PACL pDacl = nullptr;
    PSECURITY_DESCRIPTOR pSD = nullptr;

    // Check that we can get the security info.
    DWORD dwRes =
            GetNamedSecurityInfoW(token_path_.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, &pDacl, nullptr, &pSD);

    ASSERT_EQ(dwRes, ERROR_SUCCESS);
    EXPECT_NE(pDacl, nullptr);

    // We expect 2 ACE (the owner, and sysem).
    ACL_SIZE_INFORMATION aclSize;
    if (GetAclInformation(pDacl, &aclSize, sizeof(aclSize), AclSizeInformation)) {
        EXPECT_EQ(aclSize.AceCount, 2);
    }

    if (pSD) LocalFree(pSD);
}
#endif

TEST_F(TelnetAuthTest, LoadOrCreate_ConcurrentAccess) {
    const int kNumThreads = 16;
    std::vector<std::future<absl::StatusOr<TelnetAuth::Token>>> futures;

    // Let the herd take off! Create those tookens!
    for (int i = 0; i < kNumThreads; ++i) {
        futures.push_back(std::async(std::launch::async, [this]() {
            return TelnetAuth::LoadOrCreateToken(16, token_path_);
        }));
    }

    std::string first_token_str;
    for (auto& f : futures) {
        auto result = f.get();
        ASSERT_TRUE(result.ok()) << "Concurrent load failed: " << result.status();
        if (first_token_str.empty()) {
            first_token_str = std::string(result->AsStringView());
        } else {
            // Everyone must eventually see the same token (the winner of the rename)
            EXPECT_EQ(first_token_str, result->AsStringView());
        }
    }

    // Final verification of file content matches our consensus
    auto read_back = TelnetAuth::ReadToken(token_path_);
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->AsStringView(), first_token_str);
}

TEST_F(TelnetAuthTest, LoadOrCreate_ReturnsInvalidArgument_WhenEntropyIsZero) {
    const absl::StatusOr<TelnetAuth::Token> result =
            TelnetAuth::LoadOrCreateToken(/*entropy=*/0, token_path_);
    ASSERT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsInvalidArgument(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr("0 bytes"));
    EXPECT_THAT(result.status().message(), HasSubstr("at least 1 byte"));
    EXPECT_FALSE(android::base::file::exists(token_path_));
}

TEST_F(TelnetAuthTest, LoadOrCreate_SurfacesProvisionError_WhenParentDirectoryMissing) {
    const std::filesystem::path missing_dir = test_home_ / "missing_parent_dir";
    const std::filesystem::path missing_parent_token = missing_dir / "token";

    const absl::StatusOr<TelnetAuth::Token> result =
            TelnetAuth::LoadOrCreateToken(16, missing_parent_token);
    ASSERT_FALSE(result.ok());
    EXPECT_FALSE(absl::IsNotFound(result.status()));
    EXPECT_THAT(result.status().message(), HasSubstr("Failed to provision"));
    EXPECT_THAT(result.status().message(), HasSubstr("after 10 attempts"));
    EXPECT_THAT(result.status().message(), HasSubstr(missing_dir.string()));
}

TEST_F(TelnetAuthTest, LoadOrCreateAndReadToken_SupportsUnicodeFilePath) {
    const std::filesystem::path unicode_dir = test_home_ / "täst_中文";
    ASSERT_TRUE(android::base::file::mkdir_recursive(unicode_dir, 0700).ok());
    const std::filesystem::path unicode_token_path = unicode_dir / "tökën_文件";

    const absl::StatusOr<TelnetAuth::Token> missing = TelnetAuth::ReadToken(unicode_token_path);
    ASSERT_FALSE(missing.ok());
    EXPECT_TRUE(absl::IsNotFound(missing.status()));
    EXPECT_THAT(missing.status().message(), HasSubstr(unicode_token_path.string()));

    const absl::StatusOr<TelnetAuth::Token> created =
            TelnetAuth::LoadOrCreateToken(16, unicode_token_path);
    ASSERT_TRUE(created.ok()) << created.status();

    const absl::StatusOr<TelnetAuth::Token> loaded = TelnetAuth::ReadToken(unicode_token_path);
    ASSERT_TRUE(loaded.ok()) << loaded.status();
    EXPECT_EQ(loaded->AsStringView(), created->AsStringView());
}

}  // namespace goldfish::telnet
