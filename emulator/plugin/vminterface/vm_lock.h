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

namespace android::goldfish {

// In QEMU2, each virtual CPU runs on its own host threads, but all these
// threads are synchronized through a global mutex, which allows the virtual
// device code to not care about them.
//
// However, if you have to call, from any other thread, a low-level QEMU
// function that operate on virtual devices (e.g. some Android pipe-related
// functions), you must acquire the global mutex before doing so, and release
// it after that.

// This header provides a convenience interface class you can use to do
// just that, i.e.:
//
// 1) To operate on the lock, call VmLock::Get() to retrieve the
//    current VmLock instance, then invoke its Lock() and Unlock()
//    methods.
//
// 2) Glue code should call VmLock::Set() to inject their own implementation
//    into the process. The default implementation doesn't do anything.
//

class VmLock {
  public:
    VmLock() = default;
    virtual ~VmLock();
    VmLock(VmLock&&) = delete;
    VmLock(const VmLock&) = delete;
    VmLock operator=(VmLock&&) = delete;
    VmLock operator=(const VmLock&) = delete;

    // Lock the VM global mutex.
    virtual void Lock() {}

    // Unlock the VM global mutex.
    virtual void Unlock() {}

    // Returns true iff the lock is held by the current thread, false
    // otherwise. Note that for a correct implementation, that doesn't
    // only depend on the number of times that VmLock::Lock() and
    // VmLock::Unlock() were called, but also on other QEMU threads that
    // act on the global lock.
    virtual bool IsLockedBySelf() const { return true; }

    // Return current VmLock instance. Cannot return nullptr.
    // NOT thread-safe, but we don't expect multiple threads to call this
    // concurrently at init time, and the worst that can happen is to leak
    // a single instance.
    static VmLock* Get();

    // Returns whether or not there is a VmLock.
    // Does not instantiate a VmLock.
    static bool HasInstance();

    // Set new VmLock instance. Return old value, which cannot be nullptr and
    // can be deleted by the caller. If |vm_lock| is nullptr, a new default
    // instance is created. NOTE: not thread-safe with regards to Get().
    static VmLock* Set(VmLock* vm_lock);
};

// Convenience class to perform scoped VM locking.
class ScopedVmLock {
  public:
    ScopedVmLock(VmLock* vm_lock = VmLock::Get()) : vm_lock_(vm_lock) { vm_lock_->Lock(); }

    ~ScopedVmLock() { vm_lock_->Unlock(); }

  private:
    VmLock* const vm_lock_;
};

// Convenience class to perform scoped VM locking (but does not try
// to lock twice).
class RecursiveScopedVmLock {
  public:
    RecursiveScopedVmLock(VmLock* vm_lock = VmLock::Get()) {
        if (vm_lock->IsLockedBySelf()) {
            vm_lock_ = nullptr;
        } else {
            vm_lock_ = vm_lock;
            vm_lock_->Lock();
        }
    }

    ~RecursiveScopedVmLock() {
        if (vm_lock_) {
            vm_lock_->Unlock();
        }
    }

  private:
    VmLock* vm_lock_;
};

// Convenience class to perform scoped VM locking (but does not try
// to lock twice), but no-ops if there is no instance.
class RecursiveScopedVmLockIfInstance {
  public:
    RecursiveScopedVmLockIfInstance() {
        if (!VmLock::HasInstance()) return;

        VmLock* vm_lock = VmLock::Get();

        if (vm_lock->IsLockedBySelf()) {
            vm_lock_ = nullptr;
        } else {
            vm_lock_ = vm_lock;
            vm_lock_->Lock();
        }
    }

    ~RecursiveScopedVmLockIfInstance() {
        if (vm_lock_) {
            vm_lock_->Unlock();
        }
    }

  private:
    VmLock* vm_lock_ = nullptr;
};

// Another convenience class for a code that may run either under a lock or not
// but needs to ensure that some part of it runs without a VmLock.
class ScopedVmUnlock {
  public:
    ScopedVmUnlock(VmLock* vm_lock = VmLock::Get()) {
        if (vm_lock->IsLockedBySelf()) {
            vm_lock_ = vm_lock;
            vm_lock_->Unlock();
        } else {
            vm_lock_ = nullptr;
        }
    }

    ~ScopedVmUnlock() {
        if (vm_lock_) {
            vm_lock_->Lock();
        }
    }

  private:
    VmLock* vm_lock_;
};

}  // namespace android::goldfish
