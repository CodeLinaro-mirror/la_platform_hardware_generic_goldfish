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
#include "android/emulation/control/slot_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "standard-headers/linux/input-event-codes.h"

namespace android {
namespace emulation {
namespace control {

using namespace std::chrono_literals;

TEST(SlotRegistryTest, AcquireReleaseSlot) {
    SlotRegistry registry(absl::Milliseconds(10));
    int slot1 = registry.acquireSlot(1);
    ASSERT_GE(slot1, 0);
    ASSERT_TRUE(registry.isSlotRegistered(slot1));
    ASSERT_TRUE(registry.isIdentifierRegistered(1));

    registry.releaseSlot(1);
    ASSERT_FALSE(registry.isSlotRegistered(slot1));
    ASSERT_FALSE(registry.isIdentifierRegistered(1));
}

TEST(SlotRegistryTest, AcquireMultipleSlots) {
    SlotRegistry registry(absl::Milliseconds(10));
    std::vector<int> slots;
    for (int i = 0; i < MTS_POINTERS_NUM; ++i) {
        int slot = registry.acquireSlot(i);
        ASSERT_GE(slot, 0);
        slots.push_back(slot);
        ASSERT_TRUE(registry.isSlotRegistered(slot));
        ASSERT_TRUE(registry.isIdentifierRegistered(i));
    }

    // Check that we can't acquire any more slots.
    ASSERT_EQ(registry.acquireSlot(MTS_POINTERS_NUM), -1);

    // Release all slots.
    for (int i = 0; i < MTS_POINTERS_NUM; ++i) {
        registry.releaseSlot(i);
        ASSERT_FALSE(registry.isSlotRegistered(slots[i]));
        ASSERT_FALSE(registry.isIdentifierRegistered(i));
    }
}

TEST(SlotRegistryTest, ReacquireSlot) {
    SlotRegistry registry(absl::Milliseconds(10));
    int slot1 = registry.acquireSlot(1);
    ASSERT_GE(slot1, 0);
    registry.releaseSlot(1);

    // Reacquire the same slot.
    int slot2 = registry.acquireSlot(1);
    ASSERT_EQ(slot2, slot1);
}

TEST(SlotRegistryTest, ExpireOldSlots) {
    SlotRegistry registry(absl::Milliseconds(10));

    int slot1 = registry.acquireSlot(1);
    int slot2 = registry.acquireSlot(2);

    registry.updateSlotExpiration(1);
    registry.updateSlotExpiration(2);
    // Advance time past expiration
    std::this_thread::sleep_for(20ms);

    std::vector<EvDevEvent> events = registry.expireOldSlots();
    ASSERT_EQ(events.size(), 4);

    // Note that expiration of slot events depends on the hashmap ordering.

    // check the first events are the slot events
    ASSERT_EQ(events[0].type, EV_ABS);
    ASSERT_EQ(events[0].code, ABS_MT_SLOT);
    // Assert that the first slot event value is either slot1 or slot2
    ASSERT_TRUE(events[0].value == slot1 || events[0].value == slot2);

    ASSERT_EQ(events[1].type, EV_ABS);
    ASSERT_EQ(events[1].code, ABS_MT_TRACKING_ID);
    ASSERT_EQ(events[1].value, MTS_POINTER_UP);

    // check the second events are the slot events
    ASSERT_EQ(events[2].type, EV_ABS);
    ASSERT_EQ(events[2].code, ABS_MT_SLOT);
    // Assert that the first slot event value is either slot1 or slot2
    ASSERT_TRUE(events[0].value == slot1 || events[0].value == slot2);
    ASSERT_EQ(events[3].type, EV_ABS);
    ASSERT_EQ(events[3].code, ABS_MT_TRACKING_ID);
    ASSERT_EQ(events[3].value, MTS_POINTER_UP);

    ASSERT_FALSE(registry.isSlotRegistered(slot1));
    ASSERT_FALSE(registry.isIdentifierRegistered(1));
    ASSERT_FALSE(registry.isSlotRegistered(slot2));
    ASSERT_FALSE(registry.isIdentifierRegistered(2));
}

TEST(SlotRegistryTest, ExpireOldSlotsOne) {
    SlotRegistry registry(absl::Milliseconds(10));

    int slot1 = registry.acquireSlot(1);

    registry.updateSlotExpiration(1);
    // Advance time past expiration
    std::this_thread::sleep_for(20ms);

    std::vector<EvDevEvent> events = registry.expireOldSlots();
    ASSERT_EQ(events.size(), 2);

    // check the first events are the slot events
    ASSERT_EQ(events[0].type, EV_ABS);
    ASSERT_EQ(events[0].code, ABS_MT_SLOT);
    ASSERT_EQ(events[0].value, slot1);
    ASSERT_EQ(events[1].type, EV_ABS);
    ASSERT_EQ(events[1].code, ABS_MT_TRACKING_ID);
    ASSERT_EQ(events[1].value, MTS_POINTER_UP);

    ASSERT_FALSE(registry.isSlotRegistered(slot1));
    ASSERT_FALSE(registry.isIdentifierRegistered(1));
}
TEST(SlotRegistryTest, NoExpireRecent) {
    SlotRegistry registry(absl::Milliseconds(10));

    int slot1 = registry.acquireSlot(1);

    registry.updateSlotExpiration(1);
    std::vector<EvDevEvent> events = registry.expireOldSlots();
    ASSERT_EQ(events.size(), 0);

    ASSERT_TRUE(registry.isSlotRegistered(slot1));
    ASSERT_TRUE(registry.isIdentifierRegistered(1));
}
}  // namespace control
}  // namespace emulation
}  // namespace android
