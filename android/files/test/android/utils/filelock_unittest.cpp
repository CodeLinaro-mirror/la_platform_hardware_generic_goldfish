// Copyright 2018 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/utils/filelock.h"

#include "aemu/base/ArraySize.h"
#include "aemu/base/EintrWrapper.h"
#include "android/base/system/System.h"
#include "aemu/base/process/Process.h"
#include "android/base/testing/TestTempDir.h"
#include "android/utils/filelock_test.h"
#include "android/utils/path.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#ifndef _MSC_VER
#include <unistd.h>
#endif
#endif

namespace {

class FileLockTestInterface : public ::testing::Test {
public:
  static const char *getTestCaseName() {
    return ::testing::UnitTest::GetInstance()
        ->current_test_info()
        ->test_case_name();
  }

  static const char *getTestName() {
    return ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }

protected:
  virtual bool isParentProcess() const = 0;
  virtual size_t writeToOther(void *buffer, size_t size) = 0;
  virtual size_t readFromOther(void *buffer, size_t size) = 0;
  virtual void killChild() = 0;
  bool expectLockFail(int timeout = 0) {
    FileLock *lock = filelock_create_timeout(mFileLockPath.c_str(), timeout);
    EXPECT_EQ(lock, nullptr);
    bool ret = lock == nullptr;
    if (lock) {
      filelock_release(lock);
    }
    return ret;
  }
  bool expectLockSuccessAndRelease(int timeout = 0) {
    FileLock *lock = filelock_create_timeout(mFileLockPath.c_str(), timeout);
    EXPECT_NE(lock, nullptr);
    bool ret = lock != nullptr;
    if (lock) {
      filelock_release(lock);
    }
    return ret;
  }
  void assertChildSuccess() {
    ASSERT_TRUE(isParentProcess());
    uint8_t succeed = 0;
    readFromOther(&succeed, sizeof(uint8_t));
    ASSERT_EQ(succeed, 1);
  }
  bool expectReady() {
    uint8_t ready;
    readFromOther(&ready, sizeof(uint8_t));
    EXPECT_EQ(ready, 1);
    return ready;
  }
  std::unique_ptr<android::base::TestTempDir> mTempDir;
  std::string mFileLockPath;
};

#ifdef _WIN32

class FileLockTest : public FileLockTestInterface {
public:
  void ReadFromPipe(HANDLE hPipe, DWORD stdHandle) {
    char chBuf[4096];
    DWORD dwRead;
    std::string prefix = "CHLD> ";
    bool newLine = true;
    std::string line = prefix;

    while (ReadFile(hPipe, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) &&
           dwRead != 0) {
      for (DWORD i = 0; i < dwRead; ++i) {
        line += chBuf[i];
        if (chBuf[i] == '\n') {
          WriteFile(GetStdHandle(stdHandle), line.c_str(), line.size(), NULL, NULL);
          line = prefix;
        }
      }
    }
  }

  void SetUp() override {
    const char *kFileLockName = "filelock.txt";
    mStderrThread = nullptr;
    mStdoutThread = nullptr;
    if (WindowsFlags::sIsParentProcess) {
      mTempDir.reset(new android::base::TestTempDir("FileLockTest"));
      mFileLockPath = android::base::System::pathAsString(
          mTempDir->makeSubPath(kFileLockName));
      ASSERT_TRUE(mTempDir->makeSubFile(kFileLockName));
      SECURITY_ATTRIBUTES sa;
      sa.nLength = sizeof(sa);
      sa.lpSecurityDescriptor = NULL;
      sa.bInheritHandle = true;

      ASSERT_TRUE(CreatePipe(&mChildRead, &mParentWrite, &sa, 0));
      ASSERT_TRUE(CreatePipe(&mParentRead, &mChildWrite, &sa, 0));

      HANDLE hChildStd_OUT_Wr, hChildStd_OUT_Rd; // Pipes for stdout
      HANDLE hChildStd_ERR_Wr, hChildStd_ERR_Rd; // Pipes for stderr

      ASSERT_TRUE(CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0));
      ASSERT_TRUE(CreatePipe(&hChildStd_ERR_Rd, &hChildStd_ERR_Wr, &sa, 0));

      // Ensure the read handles are not inherited
      SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);
      SetHandleInformation(hChildStd_ERR_Rd, HANDLE_FLAG_INHERIT, 0);

      // Get the name of executable
      char exePath[MAX_PATH];
      ASSERT_NE(GetModuleFileNameA(NULL, exePath, MAX_PATH), MAX_PATH);
      char cmdBuffer[MAX_PATH * 2 + 100];
      ASSERT_GT(sizeof(cmdBuffer),
                snprintf(cmdBuffer, ARRAY_SIZE(cmdBuffer),
                         "%s --gtest_filter=%s.%s --child --read=%p "
                         "--write=%p "
                         "--file-lock-path=\"%s\"",
                         exePath, getTestCaseName(), getTestName(), mChildRead,
                         mChildWrite, mFileLockPath.c_str()));
      STARTUPINFOA si;
      memset(&si, 0, sizeof(si));
      memset(&mPi, 0, sizeof(mPi));
      si.cb = sizeof(si);
      si.hStdError = hChildStd_ERR_Wr;
      si.hStdOutput = hChildStd_OUT_Wr;
      si.dwFlags |= STARTF_USESTDHANDLES;

      std::cout << "Launch kid: " << cmdBuffer << std::endl;
      ASSERT_TRUE(CreateProcessA(nullptr, cmdBuffer, nullptr, nullptr, true, 0,
                                 nullptr, nullptr, &si, &mPi));

      // Close the child process's write ends of the pipes in the parent process
      CloseHandle(hChildStd_OUT_Wr);
      CloseHandle(hChildStd_ERR_Wr);

      mStdoutThread = std::make_unique<std::thread>(
          [&]() { ReadFromPipe(hChildStd_OUT_Rd, STD_OUTPUT_HANDLE); });
      mStderrThread = std::make_unique<std::thread>(
          [&]() { ReadFromPipe(hChildStd_ERR_Rd, STD_ERROR_HANDLE); });
    } else {
      mFileLockPath = WindowsFlags::sFileLockPath;
      mChildRead = WindowsFlags::sChildRead;
      mChildWrite = WindowsFlags::sChildWrite;
    }
  }
  void TearDown() override {
    CloseHandle(mChildRead);
    CloseHandle(mChildWrite);
    if (isParentProcess()) {
      CloseHandle(mParentRead);
      CloseHandle(mParentWrite);
      if (mPi.hProcess) {
        CloseHandle(mPi.hProcess);
      }
      if (mPi.hThread) {
        CloseHandle(mPi.hThread);
      }
    }
    if (mStderrThread) {
      mStderrThread->join();
    }
    if (mStdoutThread) {
      mStdoutThread->join();
    }
    if (!isParentProcess()) {
      exit(0);
    }
  }

protected:
  bool isParentProcess() const override {
    return WindowsFlags::sIsParentProcess;
  }
  size_t writeToOther(void *buffer, size_t size) override {
    HANDLE pipe = isParentProcess() ? mParentWrite : mChildWrite;
    DWORD ret = 0;
    WriteFile(pipe, buffer, size, &ret, nullptr);
    return ret;
  }
  size_t readFromOther(void *buffer, size_t size) override {
    HANDLE pipe = isParentProcess() ? mParentRead : mChildRead;
    DWORD ret = 0;
    ReadFile(pipe, buffer, size, &ret, nullptr);
    return ret;
  }
  void killChild() override {
    ASSERT_TRUE(TerminateProcess(mPi.hProcess, 0));
    if (mPi.hProcess) {
      CloseHandle(mPi.hProcess);
    }
    if (mPi.hThread) {
      CloseHandle(mPi.hThread);
    }
    memset(&mPi, 0, sizeof(mPi));
  }

  std::unique_ptr<std::thread> mStdoutThread;
  std::unique_ptr<std::thread> mStderrThread;

  HANDLE mChildRead;
  HANDLE mChildWrite;
  HANDLE mParentRead;
  HANDLE mParentWrite;
  PROCESS_INFORMATION mPi;
};

#else

class FileLockTest : public FileLockTestInterface {
public:
  void SetUp() override {
    const char *kFileLockName = "filelock.txt";

    mTempDir.reset(new android::base::TestTempDir("FileLockTest"));
    mFileLockPath = mTempDir->makeSubPath(kFileLockName);
    ASSERT_TRUE(mTempDir->makeSubFile(kFileLockName));
    ASSERT_EQ(pipe(mPipe0), 0);
    ASSERT_EQ(pipe(mPipe1), 0);
    mChildPid = fork();
    ASSERT_GE(mChildPid, 0);

    filelock_init();
  }
  void TearDown() override {
    close(mPipe0[0]);
    close(mPipe0[1]);
    close(mPipe1[0]);
    close(mPipe1[1]);
    if (!isParentProcess()) {
      exit(0);
    }
  }

protected:
  bool isParentProcess() const override { return mChildPid != 0; }
  size_t writeToOther(void *buffer, size_t size) override {
    int pipe = isParentProcess() ? mPipe0[1] : mPipe1[1];
    return write(pipe, buffer, size);
  }
  size_t readFromOther(void *buffer, size_t size) override {
    int pipe = isParentProcess() ? mPipe1[0] : mPipe0[0];
    return read(pipe, buffer, size);
  }
  void killChild() override {
    ASSERT_TRUE(isParentProcess());
    ASSERT_EQ(0, HANDLE_EINTR(kill(mChildPid, SIGKILL)));
    waitpid(mChildPid, nullptr, 0);
  }
  int mChildPid = 0;
  int mPipe0[2] = {};
  int mPipe1[2] = {};
};
#endif
} // namespace

TEST_F(FileLockTest, createConflict) {
  if (isParentProcess()) {
    // Parent process
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    ASSERT_NE(lock, nullptr);
    uint8_t ready = 1;
    writeToOther(&ready, sizeof(uint8_t));
    assertChildSuccess();
    filelock_release(lock);
  } else {
    // Child process
    uint8_t succeed = 1;
    succeed &= expectReady();
    succeed &= expectLockFail();
    // We must report the succeed bit back to parent process.
    // Or the test scheduler cannot catch it.
    writeToOther(&succeed, sizeof(uint8_t));
  }
}

TEST_F(FileLockTest, createTimeout) {
  if (isParentProcess()) {
    // Parent process
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    ASSERT_NE(lock, nullptr);
    uint8_t ready = 1;
    writeToOther(&ready, sizeof(uint8_t));
    assertChildSuccess();
    filelock_release(lock);
  } else {
    // Child process
    uint8_t succeed = 1;
    succeed &= expectReady();
    succeed &= expectLockFail(500);
    // We must report the succeed bit back to parent process.
    // Or the test scheduler cannot catch it.
    writeToOther(&succeed, sizeof(uint8_t));
  }
}

TEST_F(FileLockTest, createAndRelease) {
  if (isParentProcess()) {
    // Parent process
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    ASSERT_NE(lock, nullptr);
    filelock_release(lock);
    uint8_t ready = 1;
    writeToOther(&ready, sizeof(uint8_t));
    assertChildSuccess();
  } else {
    // Child process
    uint8_t succeed = 1;
    succeed &= expectReady();
    succeed &= expectLockSuccessAndRelease();
    // We must report the succeed bit back to parent process.
    // Or the test scheduler cannot catch it.
    writeToOther(&succeed, sizeof(uint8_t));
  }
}

TEST_F(FileLockTest, waitForRelease) {
  if (isParentProcess()) {
    // Parent process
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    ASSERT_NE(lock, nullptr);
    uint8_t ready = 1;
    writeToOther(&ready, sizeof(uint8_t));
    assertChildSuccess();

    writeToOther(&ready, sizeof(uint8_t));
    android::base::System::get()->sleepMs(500);
    filelock_release(lock);
    assertChildSuccess();
  } else {
    // Child process
    uint8_t succeed = 1;
    succeed &= expectReady();
    succeed &= expectLockFail();
    writeToOther(&succeed, sizeof(uint8_t));

    succeed &= expectReady();
    succeed &= expectLockSuccessAndRelease(1000);
    writeToOther(&succeed, sizeof(uint8_t));
  }
}

TEST_F(FileLockTest, childSuicideStaleLock) {
  if (isParentProcess()) {
    // Parent process
    uint8_t ready = 1;
    assertChildSuccess();
    expectLockFail(500);
    writeToOther(&ready, sizeof(uint8_t));
    expectLockSuccessAndRelease(2000);
  } else {

    // Child process
    uint8_t succeed = 1;
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    EXPECT_NE(lock, nullptr) << "The child should have written the lock";
    succeed &= lock != nullptr;
    writeToOther(&succeed, sizeof(uint8_t));
    expectReady();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    exit(0);
  }
}

TEST_F(FileLockTest, killChildStaleLock) {
  if (isParentProcess()) {
    // Parent process
    uint8_t ready = 1;
    assertChildSuccess();
    expectLockFail(500);
    writeToOther(&ready, sizeof(uint8_t));
    killChild();
    expectLockSuccessAndRelease(2000);
  } else {
    // Child process
    uint8_t succeed = 1;
    FileLock *lock = filelock_create(mFileLockPath.c_str());
    EXPECT_NE(lock, nullptr);
    succeed &= lock != nullptr;
    writeToOther(&succeed, sizeof(uint8_t));
    expectReady();
    android::base::System::get()->sleepMs(5000);
    // should not hit here
    ADD_FAILURE();
  }
}
