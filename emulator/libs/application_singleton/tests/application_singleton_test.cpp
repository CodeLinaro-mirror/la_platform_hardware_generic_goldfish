// Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/singleton/application_singleton.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/strings/str_cat.h"

#include "aemu/base/process/Command.h"
#include "android/base/testing/TestSystem.h"
#include "tools/cpp/runfiles/runfiles.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace goldfish::singleton {

using android::base::TestSystem;
using ::bazel::tools::cpp::runfiles::Runfiles;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Bazel specific helper to find data dependencies.
std::string RunfilesPath(const std::string& path) {
    std::string error;
    std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
    if (runfiles == nullptr) {
        ADD_FAILURE() << "Failed to create Runfiles: " << error;
        return "";
    }

    return runfiles->Rlocation(path);
}

class ApplicationSingletonTest : public ::testing::Test {
  protected:
    ApplicationSingletonTest() {}

    void SetUp() override {
        // Generate a unique app name for each test to ensure isolation.
        mAppName = "com.android.emulator.test.singleton-" + std::to_string(getpid());

        mTempDir = std::filesystem::temp_directory_path() / "application_singleton_test" /
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(mTempDir);
        auto system = android::base::System::get();
        for (auto env : envs) {
            mOldEnvs[env] = system->envGet(env);
            system->envSet(env, mTempDir.string());
        }
    }

    void TearDown() override {
        // Check if the directory exists before trying to remove it.
        if (std::filesystem::exists(mTempDir)) {
            std::filesystem::remove_all(mTempDir);
        }
        auto system = android::base::System::get();
        for (auto [k, v] : mOldEnvs) {
            system->envSet(k, v);
        }
    }

    std::vector<std::string> envs{"LOCALAPPDATA", "XDG_RUNTIME_DIR", "HOME"};
    std::unordered_map<std::string, std::string> mOldEnvs;
    std::string mAppName;
    std::filesystem::path mTempDir;

    // Helper function to get the path to the test helper executable.
    std::string getHelperPath(const std::string& helperName) {
        return RunfilesPath(
                "goldfish+/emulator/libs/application_singleton/" + helperName);
    }

    // Helper function to run the second instance test helper.
    int runSecondInstanceHelper() {
        std::string helperPath = getHelperPath("second_instance_test_helper");
        if (helperPath.empty()) {
            return -1;  // Error logged by RunfilesPath
        }

        auto proc = android::base::Command::create({helperPath, mAppName}).execute();
        return proc->exitCode();
    }
};

// Test that a single instance can be created and holds the lock.
TEST_F(ApplicationSingletonTest, PrimaryInstanceCanLock) {
    ApplicationSingleton singleton(mAppName);
    EXPECT_TRUE(singleton.isPrimaryInstance());
}

// Test that a second instance cannot acquire the lock.
TEST_F(ApplicationSingletonTest, SecondInstanceIsRejected) {
    // First instance acquires the lock.
    ApplicationSingleton primary(mAppName);
    ASSERT_TRUE(primary.isPrimaryInstance());

    // Run the helper which will try to acquire the same lock.
    int result = runSecondInstanceHelper();

    // The helper should exit with code 1, indicating it was not the primary.
    EXPECT_EQ(1, result);
}

// Test that after the primary instance is destroyed, a new instance can
// acquire the lock.
TEST_F(ApplicationSingletonTest, LockCanBeReacquiredAfterRelease) {
    {
        ApplicationSingleton primary(mAppName);
        ASSERT_TRUE(primary.isPrimaryInstance());
    }  // primary goes out of scope here, lock is released.

    // A new instance should now be able to acquire the lock.
    ApplicationSingleton secondary(mAppName);
    EXPECT_TRUE(secondary.isPrimaryInstance());
}

// Test that two instances cannot exist in the same process.
// This is a logical test of the class design.
TEST_F(ApplicationSingletonTest, TwoInstancesInSameProcess) {
    ApplicationSingleton first(mAppName);
    ASSERT_TRUE(first.isPrimaryInstance());

    ApplicationSingleton second(mAppName);
    EXPECT_FALSE(second.isPrimaryInstance());
}

// Test that if the primary instance crashes, a new instance can acquire the lock.
TEST_F(ApplicationSingletonTest, LockReleasedAfterCrash) {
    std::string helperPath = getHelperPath("crashing_test_helper");
    ASSERT_FALSE(helperPath.empty());

    // Run the crashing helper. It will acquire the lock and then crash.
    auto proc = android::base::Command::create({helperPath, mAppName}).execute();
    proc->wait_for(5s);  // Wait for the process to terminate.

    // Now, a new instance should be able to acquire the lock.
    ApplicationSingleton newInstance(mAppName);
    EXPECT_TRUE(newInstance.isPrimaryInstance());
}

}  // namespace goldfish::singleton
