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
#include "goldfish/async/event_loop.h"

namespace goldfish {
namespace async {

/**
 * @brief Returns the global event loop.
 *
 * This event loop is running on a separate thread and can be used to schedule
 * tasks that need to run on the main event loop. It is safe to call this
 * from any thread.
 *
 * NOTE: This is not a qemu thread.
 *
 * @return The global event loop.
 */
EventLoop* globalEventLoop();

namespace testing {
/**
 * @brief Sets the global event loop for testing purposes.
 *
 * This allows tests to inject a mock or a test-specific event loop to control
 * task execution and avoid dependencies on the actual qemu main looper.
 *
 * @param newLoop The event loop to set as the global one.
 */
void setGlobalEventLoopForTesting(EventLoop* newLoop);
}  // namespace testing
}  // namespace async
}  // namespace goldfish