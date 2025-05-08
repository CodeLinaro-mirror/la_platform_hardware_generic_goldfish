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
#include "android/emulation/control/input/SlotRegistry.h"

#include "absl/log/log.h"

#include "standard-headers/linux/input-event-codes.h"
namespace android {
namespace emulation {
namespace control {

int SlotRegistry::acquireSlot(uint32_t identifier) {
    int slot = 0;
    if (mIdMap.count(identifier) == 0) {
        // pick next available slot.
        slot = findNextFreeSlot();
        if (slot < 0) {
            VLOG(1) << "No slot available for" << identifier;
            return slot;
        }
        mIdMap[identifier] = slot;
        mUsedSlots.set(slot);
        auto now = absl::Now();
        mIdLastUsedEpoch[identifier] = now + mSlotExpiration;
    } else {
        slot = mIdMap[identifier];
    }

    return slot;
}

bool SlotRegistry::isIdentifierRegistered(uint32_t identifier) {
    return mIdMap.count(identifier) > 0;
}

bool SlotRegistry::isSlotRegistered(uint32_t slot) {
    return mUsedSlots.test(slot);
}

void SlotRegistry::updateSlotExpiration(uint32_t identifier) {
    assert(mIdLastUsedEpoch.count(identifier) > 0);
    auto now = absl::Now();
    mIdLastUsedEpoch[identifier] = now + mSlotExpiration;
}

void SlotRegistry::releaseSlot(uint32_t identifier) {
    if (mIdMap.count(identifier) == 0) {
        return;
    }

    uint32_t slot = mIdMap[identifier];
    mIdMap.erase(identifier);
    mUsedSlots.reset(slot);
    mIdLastUsedEpoch.erase(identifier);
}

std::vector<EvDevEvent> SlotRegistry::expireOldSlots() {
    std::vector<EvDevEvent> events;

    absl::Time now = absl::Now();
    for (auto it = mIdLastUsedEpoch.begin(); it != mIdLastUsedEpoch.end();) {
        if (it->second < now) {
            assert(mIdMap.count(it->first) > 0);
            uint32_t removeSlot = mIdMap[it->first];
            VLOG(1) << "Expiring outdated touch event identifier: " << it->first
                    << ", slot: " << removeSlot;

            // First create an up event, otherwise android kernel might get
            // confused
            events.push_back({EV_ABS, ABS_MT_SLOT, removeSlot});
            events.push_back({EV_ABS, ABS_MT_TRACKING_ID, MTS_POINTER_UP});

            // Next remove the mappings from existence.
            mIdMap.erase(it->first);
            mIdLastUsedEpoch.erase(it++);
            mUsedSlots.reset(removeSlot);
        } else {
            ++it;
        }
    }

    return events;
}

int SlotRegistry::findNextFreeSlot() {
    static_assert(MTS_POINTERS_NUM < 20, "Consider a better algorithm for finding an empty slot.");
    for (int i = 0; i < MTS_POINTERS_NUM; i++) {
        if (!mUsedSlots.test(i)) {
            return i;
        }
    }
    return -1;
}
}  // namespace control
}  // namespace emulation
}  // namespace android