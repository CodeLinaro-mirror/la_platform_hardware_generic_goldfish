#include "telnet_auth.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <aclapi.h>
#include <windows.h>
#endif

#include "android/base/system.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "goldfish/file/file.h"

namespace goldfish::telnet {

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
    auto result = TelnetAuth::ReadToken();
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsNotFound(result.status()));

    // Side effect check: File must NOT have been created
    EXPECT_FALSE(std::filesystem::exists(token_path_));
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsRequired_WhenFileMissing) {
    // If the file is missing, we still return kRequired (meaning authentication
    // will be needed once the file is provisioned).
    EXPECT_EQ(TelnetAuth::GetStatus(), AuthStatus::kRequired);

    // Side effect check: File must NOT have been created
    EXPECT_FALSE(std::filesystem::exists(token_path_));
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsDisabled_WhenFileEmpty) {
    {
        std::ofstream ofs(token_path_);
        // Create empty file
    }
    EXPECT_EQ(TelnetAuth::GetStatus(), AuthStatus::kDisabled);
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsError_WhenFileTooLarge) {
    {
        std::ofstream ofs(token_path_);
        // 1KB limit is 1024 bytes. Let's make it 1025.
        std::string large_content(1025, 'A');
        ofs << large_content;
    }
    EXPECT_EQ(TelnetAuth::GetStatus(), AuthStatus::kError);
}

TEST_F(TelnetAuthTest, GetStatus_ReturnsRequired_WhenFileIsExactly1KB) {
    {
        std::ofstream ofs(token_path_);
        // 1KB limit is 1024 bytes.
        std::string content(1024, 'A');
        ofs << content;
    }
    EXPECT_EQ(TelnetAuth::GetStatus(), AuthStatus::kRequired);
}

TEST_F(TelnetAuthTest, ReadToken_ReturnsError_WhenFileTooLarge) {
    {
        std::ofstream ofs(token_path_);
        std::string large_content(1025, 'A');
        ofs << large_content;
    }
    auto result = TelnetAuth::ReadToken();
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsInternal(result.status()));
}

TEST_F(TelnetAuthTest, ReadToken_Succeeds_WhenFileIsExactly1KB) {
    std::string content(1024, 'A');
    {
        std::ofstream ofs(token_path_);
        ofs << content;
    }
    auto result = TelnetAuth::ReadToken();
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

    EXPECT_EQ(TelnetAuth::GetStatus(), AuthStatus::kError);

    // Restore permissions so TearDown can delete it cleanly
    std::filesystem::permissions(token_path_, std::filesystem::perms::owner_all);
}

TEST_F(TelnetAuthTest, ReadToken_ReturnsError_WhenFileUnreadable) {
    {
        std::ofstream ofs(token_path_);
        ofs << "some-token";
    }
    // Make it unreadable
    std::filesystem::permissions(token_path_, std::filesystem::perms::none);

    auto result = TelnetAuth::ReadToken();
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(absl::IsInternal(result.status()));

    // Restore permissions so TearDown can delete it cleanly
    std::filesystem::permissions(token_path_, std::filesystem::perms::owner_all);
}
#endif

// --- Section 2: Existence & Provisioning ---

TEST_F(TelnetAuthTest, LoadOrCreate_ReturnsExistingToken) {
    std::string secret = "pre-existing-token";
    {
        std::ofstream ofs(token_path_);
        ofs << secret;
    }

    auto result = TelnetAuth::LoadOrCreateToken();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->AsStringView(), secret);
}

TEST_F(TelnetAuthTest, ReadToken_TrimsWhitespace) {
    std::string secret = "whitespace-token";
    {
        std::ofstream ofs(token_path_);
        ofs << "  " << secret << "\n\r\n ";
    }

    auto result = TelnetAuth::ReadToken();
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

TEST_F(TelnetAuthTest, LoadOrCreate_ProvisionsNewToken_WhenMissing) {
    ASSERT_FALSE(std::filesystem::exists(token_path_));

    auto result = TelnetAuth::LoadOrCreateToken();
    ASSERT_TRUE(result.ok());
    // WebSafeBase64 encoding of 16 bytes of entropy results in a 22-character string (no padding).
    EXPECT_EQ(result->AsStringView().length(), 22)
            << "Token should have 16 bytes of entropy (22 chars), got: [" << result->AsStringView()
            << "]";

    // Verify file was actually created and is readable
    auto read_back = TelnetAuth::ReadToken();
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->AsStringView(), result->AsStringView());
}

#ifndef _WIN32
TEST_F(TelnetAuthTest, ProvisionsWithSecurePermissions) {
    auto result = TelnetAuth::LoadOrCreateToken();
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
    auto result = TelnetAuth::LoadOrCreateToken();
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
        futures.push_back(
                std::async(std::launch::async, []() { return TelnetAuth::LoadOrCreateToken(); }));
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
    auto read_back = TelnetAuth::ReadToken();
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->AsStringView(), first_token_str);
}

}  // namespace goldfish::telnet
