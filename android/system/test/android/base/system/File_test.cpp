#include "android/base/system/File.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "absl/status/status_matchers.h"

#include "aemu/base/EintrWrapper.h"
#include "android/base/testing/TestTempDir.h"

#define EXPECT_OK(x) EXPECT_THAT(x, absl_testing::IsOk())

namespace android::base {

TEST(File, pathIsDir) {
    TestTempDir tempDir("path_opts");
    EXPECT_FALSE(file::is_dir(tempDir.path() / "foo"));
    EXPECT_FALSE(file::is_dir(tempDir.path() / "foo/"));
#ifdef _WIN32
    EXPECT_FALSE(file::is_dir(tempDir.path() / "foo\\"));
#endif

    EXPECT_TRUE(tempDir.makeSubDir("foo"));

    EXPECT_TRUE(file::is_dir(tempDir.path() / "foo"));
    EXPECT_TRUE(file::is_dir(tempDir.path() / "foo/"));
#ifdef _WIN32
    EXPECT_TRUE(file::is_dir(tempDir.path() / "foo\\"));
#endif
}

TEST(File, pathOperations) {
    TestTempDir tempDir("path_opts");
    auto fooPath = tempDir.path() / "foo";

    EXPECT_FALSE(file::exists(fooPath));
    EXPECT_FALSE(file::is_file(fooPath));
    EXPECT_FALSE(file::is_dir(fooPath));
    EXPECT_FALSE(file::can_read(fooPath));
    EXPECT_FALSE(file::can_write(fooPath));
    EXPECT_FALSE(file::can_exec(fooPath));
    EXPECT_THAT(file::file_size(fooPath), absl_testing::StatusIs(absl::StatusCode::kInternal));

    EXPECT_OK(file::touch(tempDir.path() / "foo"));

    EXPECT_TRUE(file::exists(fooPath));
    EXPECT_TRUE(file::is_file(fooPath));
    EXPECT_FALSE(file::is_dir(fooPath));

    // NOTE: Windows doesn't have 'execute' permission bits.
    // Any readable file can be executed. Also any writable file
    // is readable.
    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IRUSR | S_IWUSR | S_IXUSR)));
    EXPECT_TRUE(file::can_read(fooPath));
    EXPECT_TRUE(file::can_write(fooPath));
    EXPECT_TRUE(file::can_exec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IRUSR)));
    EXPECT_TRUE(file::can_read(fooPath));
    EXPECT_FALSE(file::can_write(fooPath));
#ifdef _WIN32
    EXPECT_TRUE(file::can_exec(fooPath));
#else
    EXPECT_FALSE(file::can_exec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IWUSR)));
#ifdef _WIN32
    EXPECT_TRUE(file::can_read(fooPath));
    EXPECT_TRUE(file::can_write(fooPath));
    EXPECT_TRUE(file::can_exec(fooPath));
#else
    EXPECT_FALSE(file::can_read(fooPath));
    EXPECT_TRUE(file::can_write(fooPath));
    EXPECT_FALSE(file::can_exec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IXUSR)));
#ifdef _WIN32
    EXPECT_TRUE(file::can_read(fooPath));
#else
    EXPECT_FALSE(file::can_read(fooPath));
#endif
    EXPECT_FALSE(file::can_write(fooPath));
    EXPECT_TRUE(file::can_exec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IRUSR | S_IWUSR)));
    EXPECT_TRUE(file::can_read(fooPath));
    EXPECT_TRUE(file::can_write(fooPath));
#ifdef _WIN32
    EXPECT_TRUE(file::can_exec(fooPath));
#else
    EXPECT_FALSE(file::can_exec(fooPath));
#endif

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IRUSR | S_IXUSR)));
    EXPECT_TRUE(file::can_read(fooPath));
    EXPECT_FALSE(file::can_write(fooPath));
    EXPECT_TRUE(file::can_exec(fooPath));

    EXPECT_FALSE(HANDLE_EINTR(chmod(fooPath.string().c_str(), S_IWUSR | S_IXUSR)));
#ifdef _WIN32
    EXPECT_TRUE(file::can_read(fooPath));
#else
    EXPECT_FALSE(file::can_read(fooPath));
#endif
    EXPECT_TRUE(file::can_write(fooPath));
    EXPECT_TRUE(file::can_exec(fooPath));

    EXPECT_THAT(file::file_size(fooPath), absl_testing::IsOkAndHolds(0U));

    std::ofstream fooFile(fooPath);
    ASSERT_TRUE(bool(fooFile));
    fooFile << "Some non-zero data";
    fooFile.close();
    EXPECT_THAT(file::file_size(fooPath), absl_testing::IsOkAndHolds(18U));

    // Test file deletion
    EXPECT_OK(file::rm(fooPath));
    EXPECT_THAT(file::file_size(fooPath), absl_testing::StatusIs(absl::StatusCode::kInternal));
}

TEST(File, scandDirEntries) {
    static const char* const kExpected[] = {"fifth", "first", "fourth", "second", "sixth", "third"};
    static const char* const kInput[] = {"first", "second", "third", "fourth", "fifth", "sixth"};
    const size_t kCount = 6;

    TestTempDir myDir("scanDirEntries");
    for (size_t n = 0; n < kCount; ++n) {
        file::touch(myDir.path() / kInput[n]);
    }

    auto entries = file::scan_dir(myDir.path());

    ASSERT_EQ(kCount, entries.size());
    for (size_t n = 0; n < kCount; ++n) {
        EXPECT_STREQ(kExpected[n], entries[n].string().c_str()) << "#" << n;
    }
}

TEST(File, scanDirEntriesWithFullPaths) {
    static const char* const kExpected[] = {"fifth", "first", "fourth", "second", "sixth", "third"};
    static const char* const kInput[] = {"first", "second", "third", "fourth", "fifth", "sixth"};
    const size_t kCount = 6;

    TestTempDir myDir("scanDirEntriesFull");
    for (size_t n = 0; n < kCount; ++n) {
        file::touch(myDir.path() / kInput[n]);
    }

    auto entries = file::scan_dir(myDir.path(), true);

    ASSERT_EQ(kCount, entries.size());
    for (size_t n = 0; n < kCount; ++n) {
        std::string expected(myDir.path().string());
        expected += fs::path::preferred_separator;
        expected += kExpected[n];
        EXPECT_STREQ(expected.c_str(), entries[n].string().c_str()) << "#" << n;
    }
}

}  // namespace android::base
