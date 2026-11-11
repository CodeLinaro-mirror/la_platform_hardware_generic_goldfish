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
#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

#include "emulator/plugin/grpc/input/contact_table.h"
#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

namespace goldfish::grpc::v2::test {

struct GuestContact {
    int slot = 0;
    int32_t tracking_id = -1;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t pressure = 0;
    uint32_t touch_major = 0;
    uint32_t touch_minor = 0;
    int32_t orientation = 0;
    uint32_t tool_type = MT_TOOL_FINGER;
    bool is_hovering = false;
};

inline void PrintTo(const GuestContact& c, std::ostream* os) {
    *os << "{slot=" << c.slot << ", tracking_id=" << c.tracking_id << ", x=" << c.x << ", y=" << c.y
        << ", pressure=" << c.pressure << ", touch_major=" << c.touch_major
        << ", touch_minor=" << c.touch_minor << ", orientation=" << c.orientation
        << ", tool_type=" << c.tool_type << ", is_hovering=" << (c.is_hovering ? "true" : "false")
        << "}";
}

inline auto ClearedKernelSlot(int slot) {
    return ::testing::AllOf(
            ::testing::Field(&GuestContact::slot, slot),
            ::testing::Field(&GuestContact::tracking_id, -1),
            ::testing::Field(&GuestContact::x, 0U), ::testing::Field(&GuestContact::y, 0U),
            ::testing::Field(&GuestContact::pressure, 0U),
            ::testing::Field(&GuestContact::touch_major, 0U),
            ::testing::Field(&GuestContact::touch_minor, 0U),
            ::testing::Field(&GuestContact::orientation, 0),
            ::testing::Field(&GuestContact::tool_type, static_cast<uint32_t>(MT_TOOL_FINGER)));
}

// Models the guest Linux kernel evdev per-slot/key cache (`input_handle_abs_event`
// and `input_handle_key_event` deduplication) in front of Android's
// `MultiTouchMotionAccumulator`, `TouchButtonAccumulator`, and
// `MultiTouchInputMapper::syncTouch`:
//   - Kernel evdev retains per-slot `ABS_MT_*` values across `ABS_MT_TRACKING_ID = -1`
//     and drops any incoming `EV_ABS` or `EV_KEY` event matching its cached value.
//   - Userspace `mSlots` updates only from events that pass kernel deduplication,
//     retaining previous axis values when `ABS_MT_TRACKING_ID < 0` marks a slot unused,
//     and latches frame state on `EV_SYN` / `SYN_REPORT`.
class ProtocolBGuestState {
  public:
    // Matches `virtio_add_queue(vdev, 64, virtio_input_handle_evt)` in QEMU's
    // `virtio-input.c`: a single `SYN_REPORT` frame (including its trailing
    // `SYN_REPORT` descriptor) exceeding 64 descriptors is dropped whole.
    static constexpr size_t kMaxVirtioInputEventsPerFrame = 64;

    // Applies raw evdev events that already include explicit `EV_SYN` / `SYN_REPORT`
    // boundaries (such as `FakePixmanDisplay::evdev_reports`).
    template <typename EventRange>
    void ApplyReport(const EventRange& events) {
        size_t in_frame = 0;
        for (const auto& ev : events) {
            ++in_frame;
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                EXPECT_LE(in_frame, kMaxVirtioInputEventsPerFrame)
                        << "SYN_REPORT frame exceeds QEMU virtio-input 64-descriptor evt queue";
                in_frame = 0;
            }
            Apply(ev.type, ev.code, ev.value);
        }
    }

    // Applies a single report payload passed to `TouchDeviceSink::WriteReport`, where
    // the sink itself appends `EV_SYN` / `SYN_REPORT`.
    template <typename EventRange>
    void ApplyFrame(const EventRange& events) {
        size_t count = 0;
        for (const auto& ev : events) {
            ++count;
            EXPECT_NE(ev.type, static_cast<uint16_t>(EV_SYN))
                    << "the sink, not the dispatcher, appends SYN_REPORT";
            Apply(ev.type, ev.code, ev.value);
        }
        EXPECT_LE(count + 1, kMaxVirtioInputEventsPerFrame)
                << "SYN_REPORT frame exceeds QEMU virtio-input 64-descriptor evt queue";
        Apply(EV_SYN, SYN_REPORT, 0);
    }

    void Apply(uint16_t type, uint16_t code, uint32_t value) {
        if (type == EV_SYN) {
            if (code != SYN_REPORT) {
                ADD_FAILURE() << "unexpected EV_SYN code " << code;
                return;
            }
            btn_tool_rubber_ = pending_btn_tool_rubber_;
            btn_stylus_ = pending_btn_stylus_;
            for (int i = 0; i < kTouchDeviceSlots; ++i) {
                GuestContact& out = reported_slots_[i];
                const GuestContact& in = userspace_slots_[i];
                out = in;
                if (!userspace_in_use_[i]) {
                    out.tracking_id = -1;
                    out.is_hovering = false;
                } else {
                    out.is_hovering = (in.tool_type != MT_TOOL_PALM && in.pressure == 0U);
                }
            }
            return;
        }
        if (type == EV_KEY) {
            const bool down = (value != 0U);
            if (code == BTN_TOOL_RUBBER) {
                if (kernel_btn_tool_rubber_ != down) {
                    kernel_btn_tool_rubber_ = down;
                    pending_btn_tool_rubber_ = down;
                }
            } else if (code == BTN_STYLUS) {
                if (kernel_btn_stylus_ != down) {
                    kernel_btn_stylus_ = down;
                    pending_btn_stylus_ = down;
                }
            } else {
                ADD_FAILURE() << "unexpected EV_KEY code " << code;
            }
            return;
        }
        if (type != EV_ABS) {
            ADD_FAILURE() << "unexpected evdev event type " << type;
            return;
        }
        if (code == ABS_MT_SLOT) {
            if (value >= kernel_slots_.size()) {
                ADD_FAILURE() << "ABS_MT_SLOT out of range: " << value;
                return;
            }
            const int next_slot = static_cast<int>(value);
            if (kernel_selected_slot_ != next_slot) {
                kernel_selected_slot_ = next_slot;
                userspace_selected_slot_ = next_slot;
            }
            return;
        }
        GuestContact& kernel_slot = kernel_slots_[kernel_selected_slot_];
        GuestContact& slot = userspace_slots_[userspace_selected_slot_];
        switch (code) {
        case ABS_MT_TRACKING_ID: {
            const auto id = static_cast<int32_t>(value);
            if (kernel_slot.tracking_id != id) {
                kernel_slot.tracking_id = id;
                if (id < 0) {
                    userspace_in_use_[userspace_selected_slot_] = false;
                } else {
                    userspace_in_use_[userspace_selected_slot_] = true;
                    slot.tracking_id = id;
                }
            }
            break;
        }
        case ABS_MT_POSITION_X:
            if (kernel_slot.x != value) {
                kernel_slot.x = value;
                slot.x = value;
            }
            break;
        case ABS_MT_POSITION_Y:
            if (kernel_slot.y != value) {
                kernel_slot.y = value;
                slot.y = value;
            }
            break;
        case ABS_MT_PRESSURE:
            if (kernel_slot.pressure != value) {
                kernel_slot.pressure = value;
                slot.pressure = value;
            }
            break;
        case ABS_MT_TOUCH_MAJOR:
            if (kernel_slot.touch_major != value) {
                kernel_slot.touch_major = value;
                slot.touch_major = value;
            }
            break;
        case ABS_MT_TOUCH_MINOR:
            if (kernel_slot.touch_minor != value) {
                kernel_slot.touch_minor = value;
                slot.touch_minor = value;
            }
            break;
        case ABS_MT_ORIENTATION: {
            const auto deg = static_cast<int32_t>(value);
            if (kernel_slot.orientation != deg) {
                kernel_slot.orientation = deg;
                slot.orientation = deg;
            }
            break;
        }
        case ABS_MT_TOOL_TYPE:
            if (kernel_slot.tool_type != value) {
                kernel_slot.tool_type = value;
                slot.tool_type = value;
            }
            break;
        case ABS_TILT_X:
        case ABS_TILT_Y:
            // Global digitizer tilt axes; ignored by Android's MultiTouchInputMapper.
            break;
        default:
            ADD_FAILURE() << "unexpected EV_ABS code " << code;
            break;
        }
    }

    // Simulates `TouchInputMapper::reconfigure(Change::DEVICE_CONFIGURATION_OVERRIDES)`
    // when `resetNeeded` is false (e.g., `VirtualDevice` touchpad hotplug): Android's
    // `MultiTouchMotionAccumulator::configure()` replaces `mSlots` with zero-initialized
    // slots and reads `ABS_MT_SLOT` without re-syncing per-slot axes via `EVIOCGMTSLOTS`.
    void ReconfigureUserspaceWithoutSlotSync() {
        userspace_slots_ = InitialSlots();
        userspace_in_use_ = {};
        reported_slots_ = InitialSlots();
        userspace_selected_slot_ = kernel_selected_slot_;
    }

    // Injects cached per-slot axis values in guest kernel `evdev` (e.g., to simulate
    // restoring a pre-fix Quickboot snapshot where an inactive slot retained stale axes).
    void SetStuckKernelSlot(int slot, GuestContact stuck) {
        stuck.slot = slot;
        kernel_slots_.at(static_cast<size_t>(slot)) = stuck;
    }

    const GuestContact& KernelSlot(int slot) const {
        return kernel_slots_.at(static_cast<size_t>(slot));
    }

    bool BtnToolRubber() const { return btn_tool_rubber_; }
    bool BtnStylus() const { return btn_stylus_; }

    std::vector<GuestContact> ActiveContacts() const {
        std::vector<GuestContact> active;
        for (const GuestContact& slot : reported_slots_) {
            if (slot.tracking_id != -1) {
                active.push_back(slot);
            }
        }
        return active;
    }

    std::vector<GuestContact> Active() const { return ActiveContacts(); }

    const GuestContact& Slot(int slot) const {
        return reported_slots_.at(static_cast<size_t>(slot));
    }

  private:
    static std::array<GuestContact, kTouchDeviceSlots> InitialSlots() {
        std::array<GuestContact, kTouchDeviceSlots> slots{};
        for (int i = 0; i < kTouchDeviceSlots; ++i) {
            slots[static_cast<size_t>(i)].slot = i;
        }
        return slots;
    }

    int kernel_selected_slot_ = 0;
    int userspace_selected_slot_ = 0;
    bool kernel_btn_tool_rubber_ = false;
    bool kernel_btn_stylus_ = false;
    bool pending_btn_tool_rubber_ = false;
    bool pending_btn_stylus_ = false;
    bool btn_tool_rubber_ = false;
    bool btn_stylus_ = false;
    std::array<GuestContact, kTouchDeviceSlots> kernel_slots_ = InitialSlots();
    std::array<GuestContact, kTouchDeviceSlots> userspace_slots_ = InitialSlots();
    std::array<bool, kTouchDeviceSlots> userspace_in_use_{};
    std::array<GuestContact, kTouchDeviceSlots> reported_slots_ = InitialSlots();
};

}  // namespace goldfish::grpc::v2::test
