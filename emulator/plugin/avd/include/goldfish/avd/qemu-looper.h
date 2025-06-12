// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "aemu/base/async/Looper.h"

namespace android {
namespace goldfish {

class QemuLooper : public android::base::Looper {
  public:
    virtual ~QemuLooper() = default;
    // Registers the current thread as a qemu thread.
    virtual void registerQemuThread() = 0;
};

// An implementation of android::base::Looper on top of the QEMU main
// event loop. There are few important things here:
//
//  1/ There is a single global QEMU event loop
//
//
//  2/ It is not possible to call the runWithDeadlineMs() method, since
//     the event loop is started in the application's main thread by
//     the executable.
QemuLooper* qemuLooper();
}  // namespace goldfish
}  // namespace android