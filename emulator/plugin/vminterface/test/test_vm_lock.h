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
    static TestVmLock* GetInstance();

    TestVmLock() : old_vm_lock_(VmLock::Set(this)), installed_(true) {}

    ~TestVmLock() override { Release(); }

    void Release() {
        if (installed_) {
            // NOTE: A value of nullptr for old_vm_lock_ is valid.
            VmLock::Set(old_vm_lock_);
            installed_ = false;
        }
    }

    void Lock() override { lock_count_++; }
    void Unlock() override { unlock_count_++; }
    bool IsLockedBySelf() const override { return lock_count_ > unlock_count_; }

    int lock_count_ = 0;
    int unlock_count_ = 0;
    VmLock* old_vm_lock_ = nullptr;
    bool installed_ = false;
};

class HostVmLock : public VmLock {
  public:
    static HostVmLock* GetInstance();

    HostVmLock() : old_vm_lock_(VmLock::Set(this)), installed_(true) {}

    ~HostVmLock() override { Release(); }

    void Release() {
        if (installed_) {
            // NOTE: A value of nullptr for old_vm_lock_ is valid.
            VmLock::Set(old_vm_lock_);
            installed_ = false;
        }
    }

    void Lock() override ABSL_NO_THREAD_SAFETY_ANALYSIS { lock_.Lock(); }
    void Unlock() override ABSL_NO_THREAD_SAFETY_ANALYSIS { lock_.Unlock(); }
    bool IsLockedBySelf() const override ABSL_NO_THREAD_SAFETY_ANALYSIS {
        if (lock_.TryLock()) {
            lock_.Unlock();
            return false;
        }
        return true;
    }

    mutable absl::Mutex lock_;
    VmLock* old_vm_lock_ = nullptr;
    bool installed_ = false;
};

}  // namespace android::goldfish
