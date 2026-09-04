// Copyright 2026 The Android Open Source Project
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

#include "qemu_display.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/display.h"

extern "C" {
#include "ui/console.h"
#include "ui/surface.h"
}

using goldfish::async::testing::TestEventLoop;
using goldfish::display::FrameInfo;
// Avoid ambiguity with the global QemuDisplay typedef from QEMU's console.h
using EmulatorDisplay = goldfish::display::QemuDisplay;

class QemuDisplayConcurrencyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        main_loop_ = TestEventLoop::Create("main");
        qemu_loop_ = TestEventLoop::Create("qemu");

        // QemuDisplay requires a non-null QemuConsole. In tests, we can provide
        // a dummy pointer since the QEMU stubs don't dereference it.
        console_ = reinterpret_cast<QemuConsole*>(0x1234);

        // QemuDisplay constructor dereferences ds->image, so we need a dummy surface.
        surface_ = new DisplaySurface();
        surface_->image = pixman_image_create_bits(PIXMAN_a8r8g8b8, 100, 100, nullptr, 100 * 4);

        display_ = std::make_shared<EmulatorDisplay>(main_loop_.get(), qemu_loop_.get(), console_,
                                                     surface_, 0);
    }

    void TearDown() override {
        if (surface_->image) {
            pixman_image_unref(surface_->image);
        }
        delete surface_;
    }

    std::unique_ptr<TestEventLoop> main_loop_;
    std::unique_ptr<TestEventLoop> qemu_loop_;
    QemuConsole* console_;
    DisplaySurface* surface_;
    std::shared_ptr<EmulatorDisplay> display_;
};

TEST_F(QemuDisplayConcurrencyTest, Lifecycle_HappyPath) {
    auto listener = display_->AddFrameListener([](const FrameInfo&) {});
    EXPECT_TRUE(display_->IsActive());
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);  // REG task posted

    // QEMU loop executes REG task
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);

    listener = {};
    EXPECT_FALSE(display_->IsActive());
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);  // UNREG task posted

    // QEMU loop executes UNREG task
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, FlappingListener_RapidAddRemove) {
    auto listener = display_->AddFrameListener([](const FrameInfo&) {});
    listener = {};

    EXPECT_FALSE(display_->IsActive());

    // QEMU hasn't run yet, but both tasks are queued.
    EXPECT_EQ(qemu_loop_->TaskCount(), 2);

    // Run both tasks. The internal state should gracefully handle
    // registering and immediately unregistering.
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, ReRegistration_Bounce) {
    auto listener1 = display_->AddFrameListener([](const FrameInfo&) {});
    listener1 = {};
    auto listener2 = display_->AddFrameListener([](const FrameInfo&) {});

    EXPECT_TRUE(display_->IsActive());
    EXPECT_EQ(qemu_loop_->TaskCount(), 3);  // REG, UNREG, REG

    // Processing the queue should leave the display in a registered state.
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, Teardown_WithPendingTask) {
    display_->AddCallback([](const FrameInfo&) {});
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);

    // Drop our strong reference.
    // The display is kept alive by the shared_ptr captured in the qemu_loop_ task.
    std::weak_ptr<EmulatorDisplay> weak_display = display_;
    display_.reset();

    EXPECT_TRUE(weak_display.lock() != nullptr);  // Still alive!

    // Running the loop will execute the registration, and then the lambda will be
    // destroyed, triggering ~QemuDisplay() on the qemu_loop_ thread.
    qemu_loop_->RunAll();

    // The object should now be fully destroyed.
    EXPECT_TRUE(weak_display.lock() == nullptr);
}

TEST_F(QemuDisplayConcurrencyTest, ThreadSafety_ConcurrentAccess) {
    // Simulate background thread aggressively connecting/disconnecting
    std::thread stress_thread([&]() {
        for (int i = 0; i < 1000; ++i) {
            auto listener = display_->AddFrameListener([](const FrameInfo&) {});
        }
    });

    // Simulate QEMU loop processing tasks concurrently
    for (int i = 0; i < 100; ++i) {
        qemu_loop_->RunOne();
    }

    stress_thread.join();

    // Clean up any remaining tasks
    qemu_loop_->RunAll();
    EXPECT_FALSE(display_->IsActive());
}

TEST_F(QemuDisplayConcurrencyTest, MultipleSubscribers_RefCounted) {
    auto listener1 = display_->AddFrameListener([](const FrameInfo&) {});
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);  // 0 -> 1 posts REG task

    auto listener2 = display_->AddFrameListener([](const FrameInfo&) {});
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);  // 1 -> 2 does NOT post additional task

    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);

    listener1 = {};
    EXPECT_TRUE(display_->IsActive());
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);  // 2 -> 1 does NOT post UNREG task

    listener2 = {};
    EXPECT_FALSE(display_->IsActive());
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);  // 1 -> 0 posts UNREG task

    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, Teardown_FromNonLoopThread_RegisteredDCL) {
    display_->AddCallback([](const FrameInfo&) {});
    qemu_loop_->RunAll();  // DCL is now registered with QEMU

    // Destroy display on the current (non-QEMU) thread
    display_.reset();

    // The destructor should have posted an unregister_and_delete task to qemu_loop_
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);

    // Executing the loop should clean up without error
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, Teardown_FromNonLoopThread_UnregisteredDCL) {
    // DCL was never registered (0 subscribers)
    display_.reset();

    // Destructor posts cleanup task
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);

    // Cleanup executes safely without calling unregister
    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}

TEST_F(QemuDisplayConcurrencyTest, InputEvents_DispatchedToQemuLoop) {
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);

    display_->SendMouseEvent(10, 20, 1);
    EXPECT_EQ(qemu_loop_->TaskCount(), 1);

    display_->SendEvDevEvent(1, 2, 3);
    EXPECT_EQ(qemu_loop_->TaskCount(), 2);

    qemu_loop_->RunAll();
    EXPECT_EQ(qemu_loop_->TaskCount(), 0);
}
