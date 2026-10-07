// Copyright 2025 The Android Open Source Project
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/status/status_macros.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/display/display.h"
#include "goldfish/display/multi_display_callbacks.h"
#include "goldfish/display/virtual_display.h"
#include "goldfish/perfetto/perfetto_categories.h"
#include "goldfish/physics/rotation.h"
#include "goldfish/physics/skin_rotation.h"
#include "qemu_display.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

using SharedDisplayImpl = std::shared_ptr<QemuDisplay>;
using WeakDisplayImpl = std::weak_ptr<QemuDisplay>;
using QemuDisplayMap = std::unordered_map<unsigned, SharedDisplayImpl>;

using SharedVirtualDisplayImpl = std::shared_ptr<VirtualDisplay>;
using WeakVirtualDisplayImpl = std::weak_ptr<VirtualDisplay>;
using VirtualDisplayMap = std::unordered_map<unsigned, SharedVirtualDisplayImpl>;

using sensors::PhysicalModel;

absl::StatusOr<SharedDisplay> IMultiDisplay::GetActiveDisplay(DisplayId display_id,
                                                              bool has_hinge) const {
    auto screen = GetDisplay(display_id);
    if (!screen.ok()) {
        return screen.status();
    }
    auto display = screen->lock();
    if (!display) {
        return absl::UnavailableError("Display is no longer active.");
    }

    if (!IsActive(display_id) && display_id == 0 && has_hinge) {
        screen = GetDisplay(1);
        if (screen.ok()) {
            display = screen->lock();
            display_id = 1;
        }
    }

    if (!display || !IsActive(display_id)) {
        return absl::UnavailableError("Display is no longer active.");
    }

    return display;
}

class MultiDisplayImpl : public IMultiDisplay {
  public:
    MultiDisplayImpl(EventLoop* loop, EventLoop* qemu_loop, bool has_hinge)
            : IMultiDisplay(loop), qemu_loop_(qemu_loop), has_hinge_(has_hinge) {}
    ~MultiDisplayImpl() override = default;

    absl::StatusOr<DisplayPtr> CreateDisplay(DisplayId display_id, uint32_t width, uint32_t height,
                                             uint32_t dpi, uint32_t flags) override {
        SharedVirtualDisplayImpl display;
        {
            const absl::MutexLock lock(display_access_);
            if (virtual_displays_.find(display_id) != virtual_displays_.end()) {
                return absl::AlreadyExistsError(
                        absl::StrFormat("Display with id %d already exists.", display_id));
            }
            ASSIGN_OR_RETURN(display, VirtualDisplay::Create(loop_, qemu_loop_, display_id, width,
                                                             height, dpi, flags));
            display->SetRotation(GetRotation());
            virtual_displays_.emplace(display_id, display);
            active_states_[display_id] = true;
        }

        VLOG(1) << "Created display: " << display_id;
        FireEvent(DisplayEvent{DisplayEvent::AddedEvent{display}});
        return display;
    }

    absl::StatusOr<DisplayPtr> CreateDisplayFromQemu(QemuConsole* console, DisplaySurface* ds,
                                                     uint8_t id) {
        SharedDisplayImpl display;
        {
            const absl::MutexLock lock(display_access_);
            if (qemu_displays_.find(id) != qemu_displays_.end()) {
                return absl::AlreadyExistsError(
                        absl::StrFormat("Display with id %d already exists.", id));
            }
            ASSIGN_OR_RETURN(display, QemuDisplay::Create(loop_, qemu_loop_, console, ds, id));
            display->SetRotation(GetRotation());
            qemu_displays_.emplace(id, display);

            VLOG(1) << "Created display: " << *display;

            if (active_states_.find(id) == active_states_.end()) {
                active_states_[id] = (id == 0);
            }
        }

        FireEvent(DisplayEvent{DisplayEvent::AddedEvent{display}});
        return display;
    }

    absl::StatusOr<DisplayPtr> GetDisplay(DisplayId display_id) const override {
        auto display = GetDisplayWeak(display_id);
        if (display.ok()) {
            return display;
        }
        return GetVirtualDisplayWeak(display_id);
    }

    bool IsActive(DisplayId display_id) const override {
        const absl::MutexLock lock(display_access_);
        auto it = active_states_.find(display_id);
        if (it != active_states_.end()) {
            return it->second;
        }
        return false;
    }

    absl::Status SetActive(DisplayId display_id, bool active) override {
        const absl::MutexLock lock(display_access_);
        if (qemu_displays_.find(display_id) == qemu_displays_.end() &&
            virtual_displays_.find(display_id) == virtual_displays_.end()) {
            return absl::NotFoundError(absl::StrFormat("Display: %d does not exist.", display_id));
        }
        active_states_[display_id] = active;
        return absl::OkStatus();
    }

    absl::StatusOr<WeakVirtualDisplayImpl> GetVirtualDisplayWeak(DisplayId display_id) const {
        const absl::MutexLock lock(display_access_);
        auto it = virtual_displays_.find(display_id);
        if (it == virtual_displays_.end()) {
            return absl::NotFoundError(absl::StrFormat("Invalid display: %d", display_id));
        }
        return it->second;
    }

    absl::StatusOr<WeakDisplayImpl> GetDisplayWeak(DisplayId display_id) const {
        const absl::MutexLock lock(display_access_);
        auto it = qemu_displays_.find(display_id);
        if (it == qemu_displays_.end()) {
            return absl::NotFoundError(absl::StrFormat("Invalid display: %d", display_id));
        }
        return it->second;
    }

    absl::Status EraseDisplay(DisplayId display_id) override {
        auto result = EraseQemuDisplay(display_id);
        if (!result.ok()) {
            result = EraseVirtualDisplay(display_id);
        }
        return result;
    }

    absl::Status EraseQemuDisplay(DisplayId display_id) {
        SharedDisplayImpl display;
        {
            const absl::MutexLock lock(display_access_);
            auto it = qemu_displays_.find(display_id);
            if (it == qemu_displays_.end()) {
                return absl::NotFoundError(absl::StrFormat(
                        "Display: %d does not exist (already removed?).", display_id));
            }
            display = std::move(it->second);
            qemu_displays_.erase(it);
            active_states_.erase(display_id);
        }
        NotifyDisplayRemoved(std::move(display), display_id);
        return absl::OkStatus();
    }

    absl::Status EraseVirtualDisplay(DisplayId display_id) {
        SharedVirtualDisplayImpl display;
        {
            const absl::MutexLock lock(display_access_);
            auto it = virtual_displays_.find(display_id);
            if (it == virtual_displays_.end()) {
                return absl::NotFoundError(absl::StrFormat(
                        "Display: %d does not exist (already removed?).", display_id));
            }
            display = std::move(it->second);
            virtual_displays_.erase(it);
            active_states_.erase(display_id);
        }
        display->Disconnect();
        NotifyDisplayRemoved(std::move(display), display_id);
        return absl::OkStatus();
    }

    bool IsEnabled() const override { return true; }

    std::vector<DisplayPtr> Displays() const override {
        const absl::MutexLock lock(display_access_);
        std::vector<DisplayPtr> displays;
        for (const auto& pair : qemu_displays_) {
            if (active_states_.at(pair.first)) {
                displays.push_back(pair.second);
            }
        }
        for (const auto& pair : virtual_displays_) {
            if (active_states_.at(pair.first)) {
                displays.push_back(pair.second);
            }
        }

        return displays;
    }

    absl::Status Save(archive::IWriter& writer) const override {
        const absl::MutexLock lock(display_access_);

        writer << static_cast<uint32_t>(virtual_displays_.size());
        for (const auto& [id, display] : virtual_displays_) {
            writer << static_cast<uint32_t>(id);
            writer << static_cast<uint32_t>(display->GetDimensions().width);
            writer << static_cast<uint32_t>(display->GetDimensions().height);
            writer << static_cast<uint32_t>(display->Dpi());
            writer << static_cast<uint32_t>(display->Flags());
        }

        writer << static_cast<uint32_t>(active_states_.size());
        for (const auto& [id, active] : active_states_) {
            writer << static_cast<uint32_t>(id);
            writer << static_cast<bool>(active);
        }

        writer << static_cast<bool>(is_folded_);
        writer << static_cast<uint32_t>(display_mode_);

        return absl::OkStatus();
    }

    absl::Status Load(archive::IReader& reader) override {
        uint32_t num_virtual = 0;
        RETURN_IF_ERROR(ReadValue(reader, num_virtual));

        for (uint32_t i = 0; i < num_virtual; ++i) {
            uint32_t id, width, height, dpi, flags;
            RETURN_IF_ERROR(ReadValue(reader, id, width, height, dpi, flags));

            auto disp = CreateDisplay(id, width, height, dpi, flags);
            if (!disp.ok()) {
                LOG(WARNING) << "Failed to recreate virtual display " << id
                             << " on load: " << disp.status();
            }
        }

        uint32_t num_active_states = 0;
        RETURN_IF_ERROR(ReadValue(reader, num_active_states));

        for (uint32_t i = 0; i < num_active_states; ++i) {
            uint32_t id;
            bool active;
            RETURN_IF_ERROR(ReadValue(reader, id, active));

            const absl::MutexLock lock(display_access_);
            active_states_[id] = active;
        }

        bool res_f = false;
        uint32_t res_m = 0;
        RETURN_IF_ERROR(ReadValue(reader, res_f, res_m));

        {
            const absl::MutexLock lock(display_access_);
            display_mode_ = res_m;
        }

        SetFolded(res_f);
        for (const auto& display : SnapshotQemuDisplays()) {
            display->OnDeviceReset();
        }
        return absl::OkStatus();
    }

    void Reset() override {
        VirtualDisplayMap removed_virtual;
        {
            const absl::MutexLock lock(display_access_);
            removed_virtual = std::move(virtual_displays_);
            virtual_displays_.clear();

            // Clear active states for virtual displays, and default QEMU displays to false (except
            // main)
            for (auto it = active_states_.begin(); it != active_states_.end();) {
                if (qemu_displays_.find(it->first) == qemu_displays_.end()) {
                    it = active_states_.erase(it);
                } else {
                    it->second = (it->first == 0);  // Display 0 active, others inactive
                    ++it;
                }
            }
            display_mode_ = 0;
        }

        for (const auto& display : SnapshotQemuDisplays()) {
            display->OnDeviceReset();
        }

        // When the AVD resets, we must clean up all virtual displays
        // as the guest OS will forget about them.
        for (auto& [id, display] : removed_virtual) {
            display->Disconnect();
            NotifyDisplayRemoved(std::move(display), id);
        }
    }

    void SetFolded(bool folded) override {
        if (has_hinge_) {
            const absl::MutexLock lock(display_access_);
            is_folded_ = folded;

            // Display 0: active when NOT closed
            active_states_[0] = !folded;

            // Other displays: active when closed
            for (const auto& pair : qemu_displays_) {
                if (pair.first != 0) {
                    active_states_[pair.first] = folded;
                }
            }
        }
    }

    bool IsFolded() const override {
        const absl::MutexLock lock(display_access_);
        return is_folded_;
    }

    void SetDisplayMode(uint32_t mode, uint32_t width, uint32_t height, uint32_t dpi,
                        uint32_t guest_mode_id) override {
        if (mode != IMultiDisplay::kDisplayModeFoldable) {
            if (IsFolded()) {
                SharedDisplay d0;
                if (auto s0 = GetDisplay(0); s0.ok()) {
                    d0 = s0->lock();
                }

                const absl::Time deadline = absl::Now() + absl::Milliseconds(2000);
                IDisplay::ScopedFrameListener unfold_listener;
                FrameId after_id = 0;
                if (d0) {
                    unfold_listener = d0->AddFrameListener([](const FrameEvent&) {});
                    (void)d0->WaitForFreshFrame(deadline);
                    after_id = d0->CurrentFrameId();
                }

                PhysicalModel& pm = ::goldfish::avd::GetAvd().GetSensorsPhysicalModel();
                if (pm.HasFoldableModel()) {
                    pm.SetTargetPosture(
                            static_cast<float>(::goldfish::sensors::FoldablePostures::kOpened),
                            PhysicalInterpolation::kStep);
                }

                // Wait for display 0 to receive a non-blank frame after setting target posture
                bool received_frame = false;
                if (d0) {
                    while (absl::Now() < deadline) {
                        auto frame_or = d0->WaitForFrame(deadline, after_id);
                        if (!frame_or.ok()) {
                            break;
                        }
                        const auto& frame = *frame_or;
                        after_id = frame->event().frame_id;
                        if (!frame->event().blank) {
                            received_frame = true;
                            break;
                        }
                    }
                }

                if (!received_frame) {
                    // TODO: maybe trigger a power down and power up to force a
                    // update, but go ahead any way, as it is usually harmless
                    LOG(INFO) << "Timeout while waiting for display 0 to receive a frame after "
                                 "setting target posture to kOpened.";
                }
            }
        }

        {
            const absl::MutexLock lock(display_access_);
            display_mode_ = mode;
        }

        auto screen = GetDisplay(0);
        if (screen.ok()) {
            if (auto display = screen->lock()) {
                if (dpi == 0) {
                    dpi = display->Dpi();
                }
                display->SetDimensions(width, height);
                if (auto con = display->GetConsole()) {
                    grpc_dpy_gfx_update_ui_info(con, width, height);
                }

                auto* avd = ::goldfish::avd::GetNullableAvd();
                if (auto device = avd ? avd->GetActiveMultiDisplayDevice() : nullptr) {
                    device->SendSetDisplay(guest_mode_id, width, height, dpi, display->Flags());
                } else {
                    LOG(WARNING) << "Dropped multidisplay message: Guest not connected.";
                }
            }
        }
    }

    uint32_t GetDisplayMode() const override {
        const absl::MutexLock lock(display_access_);
        return display_mode_;
    }

  private:
    std::vector<SharedDisplayImpl> SnapshotQemuDisplays() const {
        const absl::MutexLock lock(display_access_);
        std::vector<SharedDisplayImpl> list;
        list.reserve(qemu_displays_.size());
        for (const auto& [id, display] : qemu_displays_) {
            list.push_back(display);
        }
        return list;
    }

    void NotifyDisplayRemoved(SharedDisplay display, DisplayId display_id) {
        auto notify = [this, display = std::move(display), display_id]() {
            display->Remove();
            FireEvent(DisplayEvent{DisplayEvent::DeletedEvent{display_id}});
        };
        if (loop_ != nullptr && !loop_->IsOnLoopThread()) {
            loop_->Post(std::move(notify)).IgnoreError();
        } else {
            notify();
        }
    }

    EventLoop* const qemu_loop_;
    QemuDisplayMap qemu_displays_ ABSL_GUARDED_BY(display_access_);
    VirtualDisplayMap virtual_displays_ ABSL_GUARDED_BY(display_access_);
    std::unordered_map<DisplayId, bool> active_states_ ABSL_GUARDED_BY(display_access_);
    uint32_t display_mode_ ABSL_GUARDED_BY(display_access_) = 0;
    const bool has_hinge_;
    bool is_folded_ ABSL_GUARDED_BY(display_access_) = false;
    mutable absl::Mutex display_access_;
};

std::unique_ptr<IMultiDisplay> IMultiDisplay::Create(EventLoop* loop, EventLoop* qemu_loop,
                                                     const sensors::FoldableConfig* fc) {
    return std::make_unique<MultiDisplayImpl>(loop, qemu_loop, fc && (fc->num_hinges > 0));
}

extern "C" void grpc_dpy_gfx_update(struct DisplayChangeListener* dcl, int x, int y, int w, int h) {
    TRACE_EVENT("display", "grpc_dpy_gfx_update", "x", x, "y", y, "w", w, "h", h);
    // TODO(jansene): True multidisplay support should go over the qemu consoles, that are tied
    // to gpu0, head:%d
    auto& multi_display =
            static_cast<MultiDisplayImpl&>(::goldfish::avd::GetAvd().GetMultiDisplay());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        con = qemu_console_lookup_default();
    }
    DisplaySurface* surface_of_console = qemu_console_surface(con);
    if (!surface_of_console) {
        LOG(ERROR) << "Unable to find a display surface";
        return;
    }

    const bool is_placeholder = surface_is_placeholder(surface_of_console);
    if (is_placeholder) {
        VLOG(1) << "ignore update of place holder surface";
        return;
    }
    auto index = qemu_console_get_index(con);
    auto device = multi_display.GetDisplayWeak(index);
    if (!device.ok()) {
        LOG(ERROR) << "Unable to find a display to handle gfx changes: " << device.status();
        return;
    }

    if (auto display = device->lock()) {
        auto dims = display->GetDimensions();

        if (std::cmp_not_equal(w, dims.width) || std::cmp_not_equal(h, dims.height)) {
            if (index) {
                return;
            }
        }

        display->OnGuestFrame(surface_of_console);
    }
}

extern "C" void grpc_dpy_gfx_refresh(DisplayChangeListener* dcl) {
    // TODO(jansene): Qemu uses this to synchronize the clipboard.
}

extern "C" void grpc_dpy_gfx_switch(struct DisplayChangeListener* dcl,
                                    struct DisplaySurface* new_surface) {
    avd::AvdUniverse& avd = ::goldfish::avd::GetAvd();
    auto& multi_display = static_cast<MultiDisplayImpl&>(avd.GetMultiDisplay());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        // TODO(whollins): maybe use qemu_console_lookup_by_device_name("gpu0", head, err);
        con = qemu_console_lookup_default();
    }
    const auto index = qemu_console_get_index(con);
    auto device = multi_display.GetDisplayWeak(index);
    if (absl::IsNotFound(device.status())) {
        DisplaySurface* surface = new_surface;
        bool created_surface = false;
        if (index == 1) {
            const auto& hw = avd.Props().hw_config;
            if (hw.hw_sensor_hinge) {
                surface = qemu_create_displaysurface(hw.hw_displayRegion_0_1_width,
                                                     hw.hw_displayRegion_0_1_height);
                created_surface = true;
            }
        }
        auto status = multi_display.CreateDisplayFromQemu(con, surface, index);
        if (!status.ok()) {
            LOG(QFATAL) << "Failed to create QEMU display " << static_cast<int>(index) << ": "
                        << status.status();
        }
        if (created_surface) {
            static_cast<QemuDisplay*>(status.value().lock().get())->SetOwnedSurface(surface);
        }
        return;
    }

    if (auto display = device->lock()) {
        if (new_surface && !surface_is_placeholder(new_surface)) {
            auto dims = display->GetDimensions();
            if (std::cmp_not_equal(surface_width(new_surface), dims.width) ||
                std::cmp_not_equal(surface_height(new_surface), dims.height)) {
                if (index) {
                    return;
                }
            }
            display->SetOwnedSurface(nullptr);
        }
        display->OnSurfaceSwitch(new_surface);
    }
}

extern "C" void grpc_dpy_gfx_update_ui_info(QemuConsole* con, int w, int h) {
    QemuUIInfo info = {
        .width = static_cast<uint32_t>(w),
        .height = static_cast<uint32_t>(h),
    };

    auto index = qemu_console_get_index(con);
    if (w == 0 || h == 0) {
        if (index == 1) {
            const auto& hw = ::goldfish::avd::GetAvd().Props().hw_config;
            if (hw.hw_sensor_hinge) {
                info.width = static_cast<uint32_t>(hw.hw_displayRegion_0_1_width);
                info.height = static_cast<uint32_t>(hw.hw_displayRegion_0_1_height);
            }
        }
    }

    if (info.width != 0 && info.height != 0) {
        dpy_set_ui_info(con, &info, false);
    }
}

}  // namespace goldfish::display
