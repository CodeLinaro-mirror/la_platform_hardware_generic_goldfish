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
#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "aemu/base/async/DefaultLooper.h"
#include "aemu/base/async/Looper.h"

namespace android::base::testing {

using android::base::Looper;

/**
 * @brief A simple class to manage a Looper running in a separate thread.
 *
 * NOTE: This is a thread looper that will just continually call run, this
 * should not be used outside of tests.
 */
class SimpleThreadLooper {
  public:
    /**
     * @brief Destructor. Automatically stops the looper thread.
     */
    ~SimpleThreadLooper();

    /**
     * @brief Stops the looper thread if it's running.
     */
    void stop();

    /**
     * @brief Starts the looper thread if it's not already running.
     */
    void start();

    /**
     * @brief Returns the associated Looper.
     * @return Pointer to the Looper.
     */
    Looper* looper();

  private:
    /**
     * @brief The function executed by the looper thread.
     */
    void runLooper();

    std::atomic_bool mRunning{false};
    std::unique_ptr<std::thread> mLooperThread;
    android::base::DefaultLooper mLooper;
};

}  // namespace android::base::testing