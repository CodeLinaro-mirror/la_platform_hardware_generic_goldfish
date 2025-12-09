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

#include <stdexcept>

#ifdef _WIN32
#include <windows.h>

#include "android/base/win32_unicode_string.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <filesystem>

#include "android/goldfish/config_dirs.h"
#endif

namespace goldfish::singleton {

struct ApplicationSingleton::Impl {
#ifdef _WIN32
    HANDLE mLockHandle = NULL;
#else
    int mLockFileDescriptor = -1;
    std::filesystem::path mLockFilePath;
#endif

    ~Impl() {
#ifdef _WIN32
        if (mLockHandle) {
            // Note: The mutex is released automatically when the handle is
            // closed. We do not need to call ReleaseMutex if we own it.
            CloseHandle(mLockHandle);
        }
#else
        if (mLockFileDescriptor >= 0) {
            // The lock is released automatically when the descriptor is closed.
            close(mLockFileDescriptor);
        }
#endif
    }
};

ApplicationSingleton::ApplicationSingleton(const std::string& appName)
        : mImpl(std::make_unique<Impl>()) {
#ifdef _WIN32
    android::base::Win32UnicodeString local("Local\\\\" + appName);
    mImpl->mLockHandle = CreateMutexW(NULL, TRUE, local.c_str());
    if (mImpl->mLockHandle == NULL) {
        throw std::runtime_error("Failed to create application mutex.");
    }
    mIsLocked = (GetLastError() != ERROR_ALREADY_EXISTS);
#else
    mImpl->mLockFilePath =
            android::goldfish::ConfigDirs::getDiscoveryDirectory() / (appName + ".lock");
    mImpl->mLockFileDescriptor = open(mImpl->mLockFilePath.c_str(), O_CREAT | O_RDWR, 0600);
    if (mImpl->mLockFileDescriptor < 0) {
        throw std::runtime_error("Failed to open or create lock file.");
    }

    if (flock(mImpl->mLockFileDescriptor, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            mIsLocked = false;
        } else {
            close(mImpl->mLockFileDescriptor);
            throw std::runtime_error("Failed to acquire file lock.");
        }
    } else {
        mIsLocked = true;
    }
#endif
}

ApplicationSingleton::~ApplicationSingleton() = default;

bool ApplicationSingleton::isPrimaryInstance() const {
    return mIsLocked;
}

}  // namespace goldfish::singleton
