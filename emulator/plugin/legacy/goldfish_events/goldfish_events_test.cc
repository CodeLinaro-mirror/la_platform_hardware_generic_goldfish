// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/legacy/goldfish_events.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "goldfish/input/virtio_input_android.h"
#include "standard-headers/linux/input-event-codes.h"

struct VirtIOInputHID {
    int placeholder = 0;
};

namespace goldfish::legacy {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;

std::vector<std::vector<int>> g_sent;
std::atomic<uint64_t> g_concurrent_sent_count{0};

int RecordSentEvent(int type, int code, int value) {
    g_sent.push_back({type, code, value});
    return 0;
}

int CountConcurrentEvent(int /*type*/, int /*code*/, int /*value*/) {
    g_concurrent_sent_count.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

class GoldfishEventsTest : public ::testing::Test {
  protected:
    void SetUp() override { ResetHooks(); }
    void TearDown() override { ResetHooks(); }

    static void ResetHooks() {
        goldfish_events_set_send_handler(nullptr);
        goldfish_events_notify_active_changed(false, 0);
        goldfish_events_set_primary_touch_device(nullptr);
        g_sent.clear();
        g_concurrent_sent_count.store(0, std::memory_order_relaxed);
    }

    std::vector<std::pair<VirtIOInputHID*, uint64_t>> observed_;
    std::unique_ptr<::android::base::eventing::ScopedEventCallback<
            goldfish::input::VirtioInputActiveEventSource, goldfish::input::VirtioInputActiveEvent>>
            handle_ = ::android::base::eventing::MakeScopedCallback(
                    goldfish::input::VirtioInputActiveEvents(),
                    [this](const goldfish::input::VirtioInputActiveEvent& event) {
                        observed_.emplace_back(event.vhid, event.active_generation);
                    });
};

TEST_F(GoldfishEventsTest, InactiveByDefaultAndSendFailsWithoutDevice) {
    EXPECT_FALSE(goldfish_events_is_active());
    EXPECT_EQ(goldfish_events_get_active_generation(), 0U);
    EXPECT_EQ(goldfish_events_get_primary_touch_device(), nullptr);
    EXPECT_EQ(goldfish_events_send(EV_ABS, ABS_MT_POSITION_X, 123), -1);
    EXPECT_THAT(observed_, IsEmpty());
}

TEST_F(GoldfishEventsTest, PrimaryTouchDeviceRoundTrips) {
    VirtIOInputHID primary_dev{};
    goldfish_events_set_primary_touch_device(&primary_dev);
    EXPECT_EQ(goldfish_events_get_primary_touch_device(), &primary_dev);
    goldfish_events_set_primary_touch_device(nullptr);
    EXPECT_EQ(goldfish_events_get_primary_touch_device(), nullptr);
}

TEST_F(GoldfishEventsTest, ActiveChangeNotifiesPrimaryTouchDevice) {
    VirtIOInputHID primary_dev{};
    goldfish_events_set_primary_touch_device(&primary_dev);

    goldfish_events_notify_active_changed(true, 42);
    EXPECT_TRUE(goldfish_events_is_active());
    EXPECT_EQ(goldfish_events_get_active_generation(), 42U);

    goldfish_events_notify_active_changed(false, 43);
    EXPECT_FALSE(goldfish_events_is_active());
    EXPECT_EQ(goldfish_events_get_active_generation(), 43U);

    EXPECT_THAT(observed_, ElementsAre(Pair(&primary_dev, 42), Pair(&primary_dev, 43)));
}

TEST_F(GoldfishEventsTest, NoPrimaryTouchDeviceSkipsActiveNotification) {
    goldfish_events_notify_active_changed(true, 7);
    EXPECT_TRUE(goldfish_events_is_active());
    EXPECT_EQ(goldfish_events_get_active_generation(), 7U);
    EXPECT_THAT(observed_, IsEmpty());
}

TEST_F(GoldfishEventsTest, SendDispatchesToRegisteredHandler) {
    goldfish_events_set_send_handler(&RecordSentEvent);
    EXPECT_EQ(goldfish_events_send(EV_ABS, ABS_MT_POSITION_X, 456), 0);
    EXPECT_EQ(goldfish_events_send(EV_KEY, KEY_A, 1), 0);
    EXPECT_THAT(g_sent, ElementsAre(ElementsAre(EV_ABS, ABS_MT_POSITION_X, 456),
                                    ElementsAre(EV_KEY, KEY_A, 1)));

    goldfish_events_set_send_handler(nullptr);
    EXPECT_EQ(goldfish_events_send(EV_KEY, KEY_A, 0), -1);
    EXPECT_EQ(g_sent.size(), 2U);
}

TEST_F(GoldfishEventsTest, ConcurrentStateTransitionsAndSendsAreRaceFree) {
    handle_.reset();
    VirtIOInputHID primary_dev{};
    constexpr int kReaderCount = 4;
    constexpr uint64_t kIterations = 1000;
    std::latch start_gate(kReaderCount + 1);
    std::atomic<bool> done{false};

    std::vector<std::thread> readers;
    readers.reserve(kReaderCount);
    for (int i = 0; i < kReaderCount; ++i) {
        readers.emplace_back([&start_gate, &done]() {
            start_gate.arrive_and_wait();
            while (!done.load(std::memory_order_acquire)) {
                if (goldfish_events_is_active()) {
                    // Generation is published with release before `active`, so
                    // observing `active == true` guarantees a non-zero generation.
                    EXPECT_GT(goldfish_events_get_active_generation(), 0U);
                    (void)goldfish_events_send(EV_KEY, KEY_A, 1);
                }
                (void)goldfish_events_get_primary_touch_device();
            }
        });
    }

    std::thread writer([&start_gate, &done, &primary_dev]() {
        start_gate.arrive_and_wait();
        for (uint64_t gen = 1; gen <= kIterations; ++gen) {
            const bool active = (gen % 2) == 1;
            if (active) {
                goldfish_events_set_primary_touch_device(&primary_dev);
                goldfish_events_set_send_handler(&CountConcurrentEvent);
                goldfish_events_notify_active_changed(true, gen);
            } else {
                goldfish_events_notify_active_changed(false, gen);
                goldfish_events_set_send_handler(nullptr);
                goldfish_events_set_primary_touch_device(nullptr);
            }
        }
        done.store(true, std::memory_order_release);
    });

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }
}

}  // namespace
}  // namespace goldfish::legacy
