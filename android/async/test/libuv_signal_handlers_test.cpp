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

#include "goldfish/async/libuv_signal_handlers.h"

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace goldfish::async {

TEST(UvSignalHandlers, AddBeforeLoopStarted) {
    auto uv_loop = LibuvEventLoop::create();

    absl::Notification signal_arrived;
    auto handlers = std::make_unique<UvSignalHandlers>(*uv_loop, [&signal_arrived](int signal) {
#ifdef _WIN32
        if (signal == SIGBREAK) {
#else
        if (signal == SIGHUP) {
#endif
            signal_arrived.Notify();
        } else {
            ADD_FAILURE() << "unexpect signal received:" << signal;
        }
    });

    std::thread t([&uv_loop] { uv_loop->run(); });

    uv_pid_t pid = uv_os_getpid();
#ifdef _WIN32
    //SetConsoleCtrlHandler(NULL, TRUE);
    //GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
#else
    uv_kill(pid, SIGHUP);
#endif

    EXPECT_TRUE(signal_arrived.WaitForNotificationWithTimeout(absl::Seconds(5)));

    // Handlers must be closed before loop shutdown.
    handlers->close();
    uv_loop->shutdown();
    t.join();
}

TEST(UvSignalHandlers, AddAfterLoopStarted) {
    auto temp_uv_loop = LibuvEventLoop::create();
    auto* uv_loop = temp_uv_loop.get();
    auto loop = ThreadedEventLoop::create(std::move(temp_uv_loop));

    absl::Notification signal_arrived;
    std::unique_ptr<UvSignalHandlers> handlers;
    // In this case we have to create them on the running loop.
    ASSERT_THAT(loop->postAndWait([&handlers, &signal_arrived, uv_loop] {
            handlers = std::make_unique<UvSignalHandlers>(*uv_loop, [&signal_arrived](int signal) {
#ifdef _WIN32
            if (signal == SIGBREAK) {
#else
            if (signal == SIGHUP) {
#endif
                signal_arrived.Notify();
            } else {
                ADD_FAILURE() << "unexpect signal received:" << signal;
            }
        });
    }),
                absl_testing::IsOk());

    uv_pid_t pid = uv_os_getpid();
#ifdef _WIN32
    //SetConsoleCtrlHandler(NULL, TRUE);
    //GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
#else
    uv_kill(pid, SIGHUP);
#endif

    EXPECT_TRUE(signal_arrived.WaitForNotificationWithTimeout(absl::Seconds(5)));

    // Let the destructor close.
    handlers.reset();
    // Threaded loop doesn't need to be shutdown explicitily: loop->shutdown();
}

}  // namespace goldfish::async
