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

#include "goldfish/legacy/goldfish_gpu/renderer/render_lib_bridge.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <map>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "render-utils/gralloc_enums.h"

namespace goldfish::legacy {
namespace {

std::atomic<HostCallbacks*> g_target{nullptr};

HostCallbacks* Target() { return g_target.load(std::memory_order_acquire); }

bool TryMapUserMemory(uint64_t gpa, void* hva, uint64_t size) {
    HostCallbacks* target = Target();
    if (target == nullptr) {
        LOG(ERROR) << "goldfish_gpu: map_user_memory(gpa=0x" << std::hex << gpa << ", size=0x"
                   << size << ", hva=" << hva << ") called with no GoldfishGpu installed";
        return false;
    }
    return target->MapUserMemory(gpa, hva, size);
}

bool TryUnmapUserMemory(uint64_t gpa, uint64_t size) {
    HostCallbacks* target = Target();
    if (target == nullptr) {
        LOG(ERROR) << "goldfish_gpu: unmap_user_memory(gpa=0x" << std::hex << gpa << ", size=0x"
                   << size << ") called with no GoldfishGpu installed";
        return false;
    }
    return target->UnmapUserMemory(gpa, size);
}

void* DmaGetHostAddr(uint64_t gpa) {
    HostCallbacks* target = Target();
    return target != nullptr ? target->ResolveDmaHostAddr(gpa) : nullptr;
}

const AddressSpaceHwFuncs kHwFuncs = {
    .allocSharedHostRegion = [](uint64_t, uint64_t*) { return -1; },
    .freeSharedHostRegion = [](uint64_t) { return 0; },
    .allocSharedHostRegionLocked = [](uint64_t, uint64_t*) { return -1; },
    .freeSharedHostRegionLocked = [](uint64_t) { return 0; },
    .getPhysAddrStart = []() -> uint64_t { return 0; },
    .getPhysAddrStartLocked = []() -> uint64_t { return 0; },
    .getGuestPageSize = []() -> uint32_t { return 4096; },
    .allocSharedHostRegionFixedLocked = [](uint64_t, uint64_t) { return -1; },
};

address_space_device_control_ops MakeAddressSpaceOps() {
    address_space_device_control_ops ops = {};
    ops.gen_handle = []() -> uint32_t { return 0; };
    ops.destroy_handle = [](uint32_t) {};
    ops.tell_ping_info = [](uint32_t, uint64_t) {};
    ops.ping = [](uint32_t) {};
    ops.add_memory_mapping = [](uint64_t gpa, void* ptr, uint64_t size) -> int {
        return TryMapUserMemory(gpa, ptr, size) ? 1 : 0;
    };
    ops.remove_memory_mapping = [](uint64_t gpa, void* /*ptr*/, uint64_t size) -> int {
        return TryUnmapUserMemory(gpa, size) ? 1 : 0;
    };
    ops.get_host_ptr = &DmaGetHostAddr;
    ops.handle_to_context = [](uint32_t) -> void* { return nullptr; };
    ops.clear = []() {};
    ops.hostmem_register = [](const struct MemEntry*) -> uint64_t { return 0; };
    ops.hostmem_unregister = [](uint64_t) {};
    ops.ping_at_hva = [](uint32_t, void*) {};
    ops.register_deallocation_callback = [](void*, uint64_t,
                                            address_space_device_deallocation_callback_t) {};
    ops.run_deallocation_callbacks = [](uint64_t) {};
    ops.control_get_hw_funcs = []() -> const AddressSpaceHwFuncs* { return &kHwFuncs; };
    ops.create_instance = [](const struct AddressSpaceCreateInfo&) {};
    return ops;
}

// Thread-safe display table backing `gfxstream_multi_display_ops`. Installing
// our own table via `RenderLib::setDisplayOps()` (just as the classic AEMU
// host does) lets us flip `is_pixel_fold` for foldable AVDs and reset stale
// secondary displays before snapshot restore without modifying gfxstream's
// public `RenderLib` interface or fallback display table.
constexpr uint32_t kDeveloperDisplayIdBegin = 6;
constexpr uint32_t kMaxDisplays = 11;
constexpr uint32_t kInvalidDisplayId = 0xFFFFFFAB;
constexpr uint32_t kPowerModeOn = 2;
constexpr uint32_t kPowerModeOnSuspend = 4;

struct DisplayColorTransform {
    float mat[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct DisplayInfo {
    int32_t pos_x = 0;
    int32_t pos_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dpi = 0;
    uint32_t flags = 0;
    uint32_t cb = 0;
    // gfxstream never reads `enabled` through this table today; default to
    // on so a pre-populated foldable display reports as active.
    bool enabled = true;
    DisplayColorTransform color_transform;
    uint32_t power_mode = kPowerModeOn;
};

struct DisplayTable {
    absl::Mutex mu;
    uint32_t foldable_width ABSL_GUARDED_BY(mu) = 0;
    uint32_t foldable_height ABSL_GUARDED_BY(mu) = 0;
    bool foldable_guest_claimed ABSL_GUARDED_BY(mu) = false;
    std::map<uint32_t, DisplayInfo> displays ABSL_GUARDED_BY(mu) = {{0, DisplayInfo{}}};
};

DisplayTable& Displays() {
    static absl::NoDestructor<DisplayTable> s_table;
    return *s_table;
}

DisplayInfo MakeFoldableDisplayInfo(uint32_t width, uint32_t height) {
    DisplayInfo info;
    info.width = width;
    info.height = height;
    return info;
}

void ResetAllDisplays(uint32_t secondary_width, uint32_t secondary_height) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    table.foldable_width = secondary_width;
    table.foldable_height = secondary_height;
    table.foldable_guest_claimed = false;
    table.displays.clear();
    table.displays.emplace(0, DisplayInfo{});
    if (secondary_width != 0 && secondary_height != 0) {
        table.displays.emplace(RenderLibBridge::kExternalFoldableDisplayId,
                               MakeFoldableDisplayInfo(secondary_width, secondary_height));
    }
}

bool DisplayIsMultiDisplayEnabled() {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    return table.displays.size() > 1;
}

bool DisplayIsMultiWindow() {
    return false;
}

void DisplayGetCombinedSize(uint32_t* /*out_w*/, uint32_t* /*out_h*/) {}

bool DisplayGetInfo(uint32_t id, int32_t* x, int32_t* y, uint32_t* w, uint32_t* h, uint32_t* dpi,
                    uint32_t* flags, bool* enabled) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        if (enabled != nullptr) {
            *enabled = false;
        }
        return false;
    }
    const DisplayInfo& info = it->second;
    if (x != nullptr) *x = info.pos_x;
    if (y != nullptr) *y = info.pos_y;
    if (w != nullptr) *w = info.width;
    if (h != nullptr) *h = info.height;
    if (dpi != nullptr) *dpi = info.dpi;
    if (flags != nullptr) *flags = info.flags;
    if (enabled != nullptr) *enabled = info.enabled;
    return true;
}

bool DisplayGetNextInfo(int32_t previous_id, uint32_t* next_id, int32_t* x, int32_t* y, uint32_t* w,
                        uint32_t* h, uint32_t* dpi, uint32_t* flags, uint32_t* cb) {
    if (previous_id == std::numeric_limits<int32_t>::max()) {
        return false;
    }
    const uint32_t check_id = previous_id < 0 ? 0U : static_cast<uint32_t>(previous_id + 1);
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.lower_bound(check_id);
    if (it == table.displays.end()) {
        return false;
    }
    const DisplayInfo& info = it->second;
    if (next_id != nullptr) *next_id = it->first;
    if (x != nullptr) *x = info.pos_x;
    if (y != nullptr) *y = info.pos_y;
    if (w != nullptr) *w = info.width;
    if (h != nullptr) *h = info.height;
    if (dpi != nullptr) *dpi = info.dpi;
    if (flags != nullptr) *flags = info.flags;
    if (cb != nullptr) *cb = info.cb;
    return true;
}

int DisplayCreate(uint32_t* display_id) {
    if (display_id == nullptr ||
        (*display_id != kInvalidDisplayId && *display_id >= kMaxDisplays)) {
        return -1;
    }
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    if (auto existing = table.displays.find(*display_id); existing != table.displays.end()) {
        return 0;
    }
    if (*display_id == kInvalidDisplayId) {
        // Guest Ranchu HWC calls rcCreateDisplay (translated on the wire to
        // kInvalidDisplayId). Claim the pre-populated foldable slot 6 on the
        // first auto-allocation so a second external display still receives 7;
        // explicit create_display(&6) calls from FrameBuffer::onLoad() hit the
        // `existing` check above without marking `foldable_guest_claimed`, and
        // HWC restarts call rcDestroyDisplay(6) first which clears it.
        if (!table.foldable_guest_claimed && table.foldable_width != 0 &&
            table.foldable_height != 0 &&
            table.displays.count(RenderLibBridge::kExternalFoldableDisplayId) != 0) {
            table.foldable_guest_claimed = true;
            *display_id = RenderLibBridge::kExternalFoldableDisplayId;
            return 0;
        }
        for (uint32_t i = kDeveloperDisplayIdBegin; i < kMaxDisplays; ++i) {
            if (table.displays.find(i) == table.displays.end()) {
                *display_id = i;
                break;
            }
        }
        if (*display_id == kInvalidDisplayId) {
            return -1;
        }
    }
    if (table.displays.size() >= kMaxDisplays) {
        return -1;
    }
    table.displays.emplace(*display_id, DisplayInfo{});
    return 0;
}

void ResetPrimaryDisplayLocked(DisplayInfo* info) {
    DisplayInfo reset;
    reset.width = info->width;
    reset.height = info->height;
    reset.dpi = info->dpi;
    *info = reset;
}

int DisplayDestroy(uint32_t id) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    if (id == 0) {
        ResetPrimaryDisplayLocked(&table.displays[0]);
        return 0;
    }
    if (id == RenderLibBridge::kExternalFoldableDisplayId && table.foldable_width != 0 &&
        table.foldable_height != 0) {
        table.foldable_guest_claimed = false;
        table.displays[id] = MakeFoldableDisplayInfo(table.foldable_width, table.foldable_height);
        return 0;
    }
    table.displays.erase(id);
    return 0;
}

int DisplayGetColorBuffer(uint32_t id, uint32_t* cb) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    if (cb != nullptr) {
        *cb = it->second.cb;
    }
    return 0;
}

int DisplaySetColorBuffer(uint32_t id, uint32_t cb) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    it->second.cb = cb;
    return 0;
}

int DisplayGetColorBufferDisplay(uint32_t cb, uint32_t* out_id) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    for (const auto& [id, info] : table.displays) {
        if (info.cb == cb) {
            if (out_id != nullptr) {
                *out_id = id;
            }
            return 0;
        }
    }
    return -1;
}

int DisplayGetPose(uint32_t id, int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    const DisplayInfo& info = it->second;
    if (x != nullptr) *x = info.pos_x;
    if (y != nullptr) *y = info.pos_y;
    if (w != nullptr) *w = info.width;
    if (h != nullptr) *h = info.height;
    return 0;
}

int DisplaySetPose(uint32_t id, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t dpi) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    DisplayInfo& info = it->second;
    info.pos_x = x;
    info.pos_y = y;
    info.width = w;
    info.height = h;
    info.dpi = dpi;
    return 0;
}

int DisplayGetColorTransform(uint32_t id, float out_matrix[16]) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    if (out_matrix != nullptr) {
        std::memcpy(out_matrix, it->second.color_transform.mat, sizeof(DisplayColorTransform::mat));
    }
    return 0;
}

int DisplaySetColorTransform(uint32_t id, const float matrix[16]) {
    if (matrix == nullptr) {
        return -1;
    }
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    std::memcpy(it->second.color_transform.mat, matrix, sizeof(DisplayColorTransform::mat));
    return 0;
}

int DisplayGetPowerMode(uint32_t id, uint32_t* power_mode) {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    if (power_mode != nullptr) {
        *power_mode = it->second.power_mode;
    }
    return 0;
}

int DisplaySetPowerMode(uint32_t id, uint32_t power_mode) {
    if (power_mode > kPowerModeOnSuspend) {
        return -1;
    }
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    auto it = table.displays.find(id);
    if (it == table.displays.end()) {
        return -1;
    }
    it->second.power_mode = power_mode;
    return 0;
}

}  // namespace

void RenderLibBridge::Install(HostCallbacks* target) {
    g_target.store(target, std::memory_order_release);
}

HostCallbacks* RenderLibBridge::Current() { return Target(); }

gfxstream_dma_ops RenderLibBridge::DmaOps() {
    return gfxstream_dma_ops{
        .get_host_addr = &DmaGetHostAddr,
        .unlock = [](uint64_t) {},
    };
}

gfxstream_vm_ops RenderLibBridge::VmOps() {
    gfxstream_vm_ops vm_ops = {};
    vm_ops.map_user_memory = [](uint64_t gpa, void* hva, uint64_t size) {
        TryMapUserMemory(gpa, hva, size);
    };
    vm_ops.unmap_user_memory = [](uint64_t gpa, uint64_t size) { TryUnmapUserMemory(gpa, size); };
    vm_ops.unmap_user_memory_async = vm_ops.unmap_user_memory;
    vm_ops.lookup_user_memory = &DmaGetHostAddr;
    vm_ops.register_vulkan_instance = [](uint64_t, const char*) {};
    vm_ops.unregister_vulkan_instance = [](uint64_t) {};
    vm_ops.set_skip_snapshot_save = [](bool) {};
    vm_ops.set_skip_snapshot_save_reason = [](uint32_t) {};
    vm_ops.set_snapshot_uses_vulkan = []() {};
    vm_ops.add_crash_reporter_log = [](const char*) {};
    return vm_ops;
}

gfxstream_multi_display_ops RenderLibBridge::DisplayOps(bool pixel_fold) {
    return gfxstream_multi_display_ops{
        .is_multi_display_enabled = &DisplayIsMultiDisplayEnabled,
        .is_multi_window = &DisplayIsMultiWindow,
        .is_pixel_fold = pixel_fold ? +[]() { return true; } : +[]() { return false; },
        .get_combined_size = &DisplayGetCombinedSize,
        .get_display_info = &DisplayGetInfo,
        .get_next_display_info = &DisplayGetNextInfo,
        .create_display = &DisplayCreate,
        .destroy_display = &DisplayDestroy,
        .get_display_color_buffer = &DisplayGetColorBuffer,
        .set_display_color_buffer = &DisplaySetColorBuffer,
        .get_color_buffer_display = &DisplayGetColorBufferDisplay,
        .get_display_pose = &DisplayGetPose,
        .set_display_pose = &DisplaySetPose,
        .get_color_transform_matrix = &DisplayGetColorTransform,
        .set_color_transform_matrix = &DisplaySetColorTransform,
        .get_display_power_mode = &DisplayGetPowerMode,
        .set_display_power_mode = &DisplaySetPowerMode,
    };
}

void RenderLibBridge::ResetSecondaryDisplays() {
    DisplayTable& table = Displays();
    absl::MutexLock lock(&table.mu);
    table.foldable_guest_claimed = false;
    for (auto it = table.displays.begin(); it != table.displays.end();) {
        if (it->first == 0) {
            ResetPrimaryDisplayLocked(&it->second);
            ++it;
        } else if (it->first == kExternalFoldableDisplayId && table.foldable_width != 0 &&
                   table.foldable_height != 0) {
            it->second = MakeFoldableDisplayInfo(table.foldable_width, table.foldable_height);
            ++it;
        } else {
            it = table.displays.erase(it);
        }
    }
}

address_space_device_control_ops* RenderLibBridge::AddressSpaceOps() {
    // gfxstream keeps the pointer, so the table must outlive every RenderLib.
    static address_space_device_control_ops s_ops = MakeAddressSpaceOps();
    return &s_ops;
}

uint64_t RenderLibBridge::SyncCreateTimeline() {
    HostCallbacks* target = Target();
    return target != nullptr ? target->CreateSyncTimeline() : 0;
}

int RenderLibBridge::SyncCreateFence(uint64_t /*timeline*/, uint32_t /*pt*/) { return 0; }

void RenderLibBridge::SyncTimelineInc(uint64_t timeline, uint32_t count) {
    if (HostCallbacks* target = Target()) {
        target->SyncTimelineInc(timeline, count);
    }
}

void RenderLibBridge::SyncDestroyTimeline(uint64_t /*timeline*/) {}

void RenderLibBridge::SyncRegisterTriggerWait(gfxstream_sync_trigger_wait_t trigger_fn) {
    if (HostCallbacks* target = Target()) {
        target->RegisterSyncTriggerWait(trigger_fn);
    }
}

bool RenderLibBridge::SyncDeviceExists() {
    HostCallbacks* target = Target();
    return target != nullptr && target->SyncDeviceExists();
}

void RenderLibBridge::Configure(gfxstream::RenderLib* render_lib, uint32_t guest_api_level,
                                SelectedRenderer renderer, uint32_t secondary_width,
                                uint32_t secondary_height) {
    const bool pixel_fold = secondary_width != 0 && secondary_height != 0;
    ResetAllDisplays(secondary_width, secondary_height);
    render_lib->setGuestAndroidApiLevel(static_cast<int>(guest_api_level));
    render_lib->setGrallocImplementation(GOLDFISH_GRALLOC);
    render_lib->setDmaOps(DmaOps());
    render_lib->setSyncDevice(&SyncCreateTimeline, &SyncCreateFence, &SyncTimelineInc,
                              &SyncDestroyTimeline, &SyncRegisterTriggerWait, &SyncDeviceExists);
    render_lib->setAddressSpaceDeviceControlOps(AddressSpaceOps());
    render_lib->setVmOps(VmOps());
    render_lib->setRenderer(renderer);
    render_lib->setDisplayOps(DisplayOps(pixel_fold));
}

}  // namespace goldfish::legacy
