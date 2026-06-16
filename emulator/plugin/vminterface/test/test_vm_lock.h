// Copyright 2016 The Android Open Source Project
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

#include "absl/synchronization/mutex.h"

#include "emulator/plugin/vminterface/vm_lock.h"

namespace android::goldfish {

class TestVmLock : public VmLock {
  public:
    static TestVmLock* getInstance();

    TestVmLock() : mOldVmLock(VmLock::set(this)), mInstalled(true) {}

    ~TestVmLock() { release(); }

    void release() {
        if (mInstalled) {
            // NOTE: A value of nullptr for mOldVmLock is valid.
            VmLock::set(mOldVmLock);
            mInstalled = false;
        }
    }

    void lock() override { mLockCount++; }
    void unlock() override { mUnlockCount++; }
    bool isLockedBySelf() const override { return mLockCount > mUnlockCount; }

    int mLockCount = 0;
    int mUnlockCount = 0;
    VmLock* mOldVmLock = nullptr;
    bool mInstalled = false;
};

class HostVmLock : public VmLock {
  public:
    static HostVmLock* getInstance();

    HostVmLock() : mOldVmLock(VmLock::set(this)), mInstalled(true) {}

    ~HostVmLock() { release(); }

    void release() {
        if (mInstalled) {
            // NOTE: A value of nullptr for mOldVmLock is valid.
            VmLock::set(mOldVmLock);
            mInstalled = false;
        }
    }

    void lock() override ABSL_NO_THREAD_SAFETY_ANALYSIS { mLock.lock(); }
    void unlock() override ABSL_NO_THREAD_SAFETY_ANALYSIS { mLock.unlock(); }
    bool isLockedBySelf() const override ABSL_NO_THREAD_SAFETY_ANALYSIS {
        if (mLock.TryLock()) {
            mLock.unlock();
            return false;
        }
        return true;
    }

    mutable absl::Mutex mLock;
    VmLock* mOldVmLock = nullptr;
    bool mInstalled = false;
};

}  // namespace android::goldfish
