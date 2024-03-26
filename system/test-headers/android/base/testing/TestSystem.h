// Copyright (C) 2015 The Android Open Source Project
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

#pragma once

#include <filesystem>
#include <string_view>

#include "aemu/base/Log.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/threads/Thread.h"
#include "android/base/system/System.h"
#include "android/base/testing/TestTempDir.h"

namespace android {
namespace base {

namespace fs = std::filesystem;

// The TestSystem class provides a mock implementation that of the System that
// can be used by UnitTests. Instantiation will result in tempory replacement of
// the System singleton with this version, the prevous setting will be restored
// in the destructor of this object
//
// Some things to be aware of:
//
// Interaction with the filesystem will result in the creation of
// a temporary directory that will be used to interact with the
// filesystem. This temporary directory is deleted in the destructor.
//
// Path resolution is done as follows:
//   - Relative paths are resolved starting from current directory
//     which by default is: "/home"
//   - The construction of this object does not create the launcherDir,
//     appDataDir or home dir. If you need these directories to exist you will
//     have to create them as follows: getTempRoot()->makeSubDir("home").
//   - Path resolution can result in switching / into \ when running under
//      Win32. If you are doing anything with paths
//     it is best to include "android/utils/path.h" and use the PATH_SEP
//     and PATH_SEP_C macros to indicate path separators in your strings.
//
class TestSystem : public System {
public:
  using System::getEnvironmentVariable;
  using System::getProgramDirectoryFromPlatform;
  using System::setEnvironmentVariable;

  TestSystem(fs::path launcherDir, fs::path homeDir = "/home",
             fs::path appDataDir = "")
      : mProgramDir(launcherDir), mProgramSubdir(""), mLauncherDir(launcherDir),
        mHomeDir(homeDir), mAppDataDir(appDataDir), mCurrentDir(),
        mIsRemoteSession(false), mRemoteSessionType(), mTempDir(NULL),
        mTempRootPrefix(), mEnvPairs(),
        mPrevSystem(System::setForTesting(this)), mTimes(), mShellOpaque(NULL),
        mUnixTime() {}

  virtual ~TestSystem() {
    System::setForTesting(mPrevSystem);
    delete mTempDir;
  }

  virtual const fs::path getProgramDirectory() const override {
    return mProgramDir;
  }

  // Set directory of currently executing binary.  This must be a subdirectory
  // of mLauncherDir and specified relative to mLauncherDir
  void setProgramSubDir(fs::path programSubDir) {
    mProgramSubdir = programSubDir;
    if (programSubDir.empty()) {
      mProgramDir = getLauncherDirectory();
    } else {
      mProgramDir = getLauncherDirectory() / programSubDir;
    }
  }

  virtual const fs::path getLauncherDirectory() const override {
    if (!mLauncherDir.empty()) {
      return mLauncherDir;
    } else {
      return getTempRoot()->pathString();
    }
  }

  void setLauncherDirectory(std::string_view launcherDir) {
    mLauncherDir = launcherDir;
    // Update directories that are suffixes of |mLauncherDir|.
    setProgramSubDir(mProgramSubdir);
  }

  virtual const fs::path getHomeDirectory() const override { return mHomeDir; }

  void setHomeDirectory(std::string_view homeDir) { mHomeDir = homeDir; }

  virtual const fs::path getAppDataDirectory() const override {
    return mAppDataDir;
  }

  void setAppDataDirectory(std::string_view appDataDir) {
    mAppDataDir = appDataDir;
  }

  virtual fs::path getCurrentDirectory() const override { return mCurrentDir; }

  virtual bool setCurrentDirectory(fs::path path) override {
    mCurrentDir = path;
    return true;
  }

  virtual OsType getOsType() const override { return mOsType; }

  virtual std::string getOsName() override { return mOsName; }

  virtual std::string getMajorOsVersion() const override { return "0.0"; }

  int getCpuCoreCount() const override { return mCoreCount; }

  void setCpuCoreCount(int count) { mCoreCount = count; }

  virtual MemUsage getMemUsage() const override {
    MemUsage res;
    res.resident = 4294967295ULL;
    res.resident_max = 4294967295ULL * 2;
    res.virt = 4294967295ULL * 4;
    res.virt_max = 4294967295ULL * 8;
    res.total_phys_memory = 4294967295ULL * 16;
    res.total_page_file = 4294967295ULL * 32;
    return res;
  }

  void setOsType(OsType type) { mOsType = type; }

  virtual std::string envGet(std::string_view varname) const override {
    for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
      const fs::path name = mEnvPairs[n];
      if (name == varname) {
        return mEnvPairs[n + 1];
      }
    }
    return std::string();
  }

  virtual std::vector<std::string> envGetAll() const override {
    std::vector<std::string> res;
    for (size_t i = 0; i < mEnvPairs.size(); i += 2) {
      const std::string name = mEnvPairs[i];
      const std::string val = mEnvPairs[i + 1];
      res.push_back(name + "=" + val);
    }
    return res;
  }

  virtual void envSet(const std::string &varname,
                      const std::string &varvalue) override {
    // First, find if the name is in the array.
    int index = -1;
    for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
      if (mEnvPairs[n] == varname) {
        index = static_cast<int>(n);
        break;
      }
    }
    if (varvalue.empty()) {
      // Remove definition, if any.
      if (index >= 0) {
        mEnvPairs.erase(mEnvPairs.begin() + index,
                        mEnvPairs.begin() + index + 2);
      }
    } else {
      if (index >= 0) {
        // Replacement.
        mEnvPairs[index + 1] = varvalue;
      } else {
        // Addition.
        mEnvPairs.emplace_back(varname);
        mEnvPairs.emplace_back(varvalue);
      }
    }
  }

  virtual bool envTest(std::string_view varname) const override {
    for (size_t n = 0; n < mEnvPairs.size(); n += 2) {
      const fs::path name = mEnvPairs[n];
      if (name == varname) {
        return true;
      }
    }
    return false;
  }

  virtual bool pathExists(fs::path path) const override {
    return pathExistsInternal(toTempRoot(path));
  }

  virtual bool pathIsFile(fs::path path) const override {
    return pathIsFileInternal(toTempRoot(path));
  }

  virtual bool pathIsDir(fs::path path) const override {
    return pathIsDirInternal(toTempRoot(path));
  }

  virtual bool pathIsLink(fs::path path) const override {
    return pathIsLinkInternal(toTempRoot(path));
  }

  virtual bool pathIsQcow2(fs::path path) const override {
    return pathIsQcow2Internal(toTempRoot(path));
  }

  virtual bool pathFileSystemIsExt4(fs::path path) const override {
    return pathFileSystemIsExt4Internal(toTempRoot(path));
  }

  virtual bool pathIsExt4(fs::path path) const override {
    return pathIsExt4Internal(toTempRoot(path));
  }

  virtual bool pathCanRead(fs::path path) const override {
    return pathCanReadInternal(toTempRoot(path));
  }

  virtual bool pathCanWrite(fs::path path) const override {
    return pathCanWriteInternal(toTempRoot(path));
  }

  virtual bool pathCanExec(fs::path path) const override {
    return pathCanExecInternal(toTempRoot(path));
  }

  virtual int pathOpen(const char *filename, int oflag,
                       int pmode) const override {
    return pathOpenInternal(filename, oflag, pmode);
  }

  virtual bool deleteFile(fs::path path) const override {
    return deleteFileInternal(toTempRoot(path));
  }

  virtual bool pathFileSize(fs::path path,
                            FileSize *outFileSize) const override {
    return pathFileSizeInternal(toTempRoot(path), outFileSize);
  }

  virtual FileSize recursiveSize(fs::path path) const override {
    return recursiveSizeInternal(toTempRoot(path));
  }

  virtual bool pathFreeSpace(fs::path path,
                             FileSize *sizeInBytes) const override {
    return pathFreeSpaceInternal(toTempRoot(path), sizeInBytes);
  }

  virtual bool fileSize(int fd, FileSize *outFileSize) const override {
    return fileSizeInternal(fd, outFileSize);
  }

  virtual std::optional<Duration>
  pathCreationTime(fs::path path) const override {
    return pathCreationTimeInternal(toTempRoot(path));
  }

  virtual std::optional<Duration>
  pathModificationTime(fs::path path) const override {
    return pathModificationTimeInternal(toTempRoot(path));
  }

  std::optional<DiskKind> pathDiskKind(fs::path path) override {
    return diskKindInternal(toTempRoot(path));
  }
  std::optional<DiskKind> diskKind(int fd) override {
    return diskKindInternal(fd);
  }

  std::vector<fs::path> scanDirEntries(fs::path dirPath,
                                       bool fullPath = false) const override {
    getTempRoot(); // make sure we have a temp root;

    std::string newPath = toTempRoot(dirPath);
    auto result = scanDirInternal(newPath);
    if (fullPath) {
      for (size_t n = 0; n < result.size(); ++n) {
        result[n] = dirPath / result[n];
      }
    }
    return result;
  }

  virtual TestTempDir *getTempRoot() const {
    if (!mTempDir) {
      mTempDir = new TestTempDir("TestSystem");
      mTempRootPrefix = mTempDir->path();
    }
    return mTempDir;
  }

  virtual bool isRemoteSession(std::string *sessionType) const override {
    if (!mIsRemoteSession) {
      return false;
    }
    *sessionType = mRemoteSessionType;
    return true;
  }

  // Force the remote session type. If |sessionType| is NULL or empty,
  // this sets the session as local. Otherwise, |*sessionType| must be
  // a session type.
  void setRemoteSessionType(std::string_view sessionType) {
    mIsRemoteSession = !sessionType.empty();
    if (mIsRemoteSession) {
      mRemoteSessionType = sessionType;
    }
  }

  virtual Times getProcessTimes() const override { return mTimes; }

  void setProcessTimes(const Times &times) { mTimes = times; }

  virtual fs::path getTempDir() const override { return "/tmp"; }

  bool getEnableCrashReporting() const override { return true; }

  virtual time_t getUnixTime() const override {
    return getUnixTimeUs() / 1000000;
  }

  virtual Duration getUnixTimeUs() const override { return getHighResTimeUs(); }

  virtual WallDuration getHighResTimeUs() const override {
    if (mUnixTimeLive) {
      auto now = hostSystem()->getHighResTimeUs();
      mUnixTime += now - mUnixTimeLastQueried;
      mUnixTimeLastQueried = now;
    }
    return mUnixTime;
  }

  void setUnixTime(time_t time) { setUnixTimeUs(time * 1000000LL); }

  void setUnixTimeUs(Duration time) { mUnixTimeLastQueried = mUnixTime = time; }

  void setLiveUnixTime(bool enable) {
    mUnixTimeLive = enable;
    if (enable) {
      mUnixTimeLastQueried = hostSystem()->getHighResTimeUs();
    }
  }

  virtual void sleepMs(unsigned n) const override {
    // Don't sleep in tests, use the static functions from Thread class
    // if you need a delay (you don't!).
    Thread::yield(); // Add a small delay to mimic the intended behavior.
  }

  virtual void sleepUs(unsigned n) const override { sleepMs(n / 1000); }

  virtual void sleepToUs(WallDuration absTime) const override {
    // Don't sleep in tests, use the static functions from Thread class
    // if you need a delay (you don't!).
    Thread::yield(); // Add a small delay to mimic the intended behavior.
  }

  virtual void yield() const override { Thread::yield(); }

  virtual void configureHost() const override {}

  System *host() { return hostSystem(); }

private:
  std::string toTempRoot(fs::path path) const {
    if (!path.is_absolute()) {
      auto currdir = getCurrentDirectory();
      path = currdir / path;
    }

    if (path.string().starts_with(mTempRootPrefix.string()))
      return path;

    fs::path combined = mTempRootPrefix;
    return mTempRootPrefix / path.relative_path();
  }

  fs::path mProgramDir;
  fs::path mProgramSubdir;
  fs::path mLauncherDir;
  fs::path mHomeDir;
  fs::path mAppDataDir;
  fs::path mCurrentDir;
  bool mIsRemoteSession;
  std::string mRemoteSessionType;
  mutable TestTempDir *mTempDir;
  mutable fs::path mTempRootPrefix;
  std::vector<std::string> mEnvPairs;
  System *mPrevSystem;
  Times mTimes;
  void *mShellOpaque;
  mutable Duration mUnixTime;
  mutable Duration mUnixTimeLastQueried = 0;
  bool mUnixTimeLive = false;
  OsType mOsType = OsType::Windows;
  std::string mOsName = "";
  bool mUnderWine = false;
  int mCoreCount = 4;
  std::optional<std::string> mWhich;
};

} // namespace base
} // namespace android
