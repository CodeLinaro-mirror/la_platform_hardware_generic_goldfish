// Copyright 2025 The Android Open Source Project
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

#include "VmLock.h"

#include <memory>

#include "absl/synchronization/mutex.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
}

// IWYU pragma: end_keep
// clang-format on

namespace android::goldfish {

namespace {

/**
 * @brief QemuVmLock class implementing the VmLock interface for QEMU.
 *
 * This class provides a concrete implementation of the VmLock interface
 * for interacting with the QEMU global mutex.
 */
class QemuVmLock : public VmLock {
  public:
    QemuVmLock() = default;
    ~QemuVmLock() override = default;

    /**
     * @brief Locks the QEMU global mutex if needed.
     */
    void lock() override {
        if (!isLockedBySelf()) {
            bql_lock();
        }
    }

    /**
     * @brief Unlocks the QEMU global mutex if locked.
     */
    void unlock() override {
        if (isLockedBySelf()) {
            bql_unlock();
        }
    }

    /**
     * @brief Checks if the QEMU global mutex is locked by the current thread.
     *
     * @return True if the mutex is locked by the current thread, false otherwise.
     */
    bool isLockedBySelf() const override { return bql_locked(); }
};

}  // namespace

static absl::Mutex sInstanceMutex;  // protects sInstance
static VmLock* sInstance ABSL_GUARDED_BY(sInstanceMutex) = nullptr;

VmLock* VmLock::get() {
    absl::MutexLock lock(&sInstanceMutex);
    if (!sInstance) {
        sInstance = new QemuVmLock();
    }
    return sInstance;
}

bool VmLock::hasInstance() {
    absl::MutexLock lock(&sInstanceMutex);
    return sInstance != nullptr;
}

VmLock::~VmLock() {}

// Mainly used for testing.
VmLock* VmLock::set(VmLock* vmLock) {
    absl::MutexLock lock(&sInstanceMutex);
    VmLock* old = sInstance;
    sInstance = vmLock;
    return old;
}

}  // namespace android::goldfish
