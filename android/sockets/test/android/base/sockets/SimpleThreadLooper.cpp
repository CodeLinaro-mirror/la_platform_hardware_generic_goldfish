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
#include "android/base/sockets/SimpleThreadLooper.h"

namespace android::base::testing {

SimpleThreadLooper::~SimpleThreadLooper() {
    stop();
}

void SimpleThreadLooper::stop() {
    if (!mRunning) {
        return;
    }

    mRunning = false;
    if (mLooperThread) {
        mLooperThread->join();
    }
}

void SimpleThreadLooper::start() {
    if (mRunning.exchange(true)) {
        return;
    }
    mLooperThread = std::make_unique<std::thread>([this] { runLooper(); });
}

Looper* SimpleThreadLooper::looper() {
    return &mLooper;
}

void SimpleThreadLooper::runLooper() {
    while (mRunning) {
        mLooper.runWithTimeoutMs(0);
    }

    // Flush the remaining events..
    bool done = mLooper.runWithTimeoutMs(100) == EWOULDBLOCK;
    for (int i = 0; i < 10 || !done; i++) {
        done = mLooper.runWithTimeoutMs(100) == EWOULDBLOCK;
    }
}

}  // namespace android::base::testing