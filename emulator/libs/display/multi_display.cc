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

#include "goldfish/display/QemuMultidisplay/multi_display.h"

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

#include "emulator/libs/display/QemuDisplay.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/display/display.h"
#include "goldfish/display/multi_display_callbacks.h"
#include "goldfish/display/virtual_display.h"
#include "goldfish/physics/rotation.h"
#include "goldfish/physics/skin_rotation.h"

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

    if (!display->Active() && display_id == 0 && has_hinge) {
        screen = GetDisplay(1);
        if (screen.ok()) {
            display = screen->lock();
        }
    }

    if (!display) {
        return absl::UnavailableError("Display is no longer active.");
    }

    return display;
}

class MultiDisplayImpl : public IMultiDisplay {
  public:
    MultiDisplayImpl(EventLoop* loop, EventLoop* qemu_loop)
            : IMultiDisplay(loop), qemu_loop_(qemu_loop) {}
    ~MultiDisplayImpl() override = default;

    absl::StatusOr<DisplayPtr> CreateDisplay(DisplayId display_id, uint32_t width, uint32_t height,
                                             uint32_t dpi, uint32_t flags) override {
        const absl::MutexLock lock(display_access_);
        auto display = std::make_shared<VirtualDisplay>(loop_, qemu_loop_, display_id, width,
                                                        height, dpi, flags);
        auto [it, inserted] = virtual_displays_.insert({display_id, display});
        if (!inserted) {
            return absl::AlreadyExistsError(
                    absl::StrFormat("Display with id %d already exists.", display_id));
        }

        VLOG(1) << "Created display: " << display_id;
        FireEvent(DisplayEvent{DisplayEvent::AddedEvent{display}});
        return display;
    }

    absl::StatusOr<DisplayPtr> CreateDisplayFromQemu(QemuConsole* console, DisplaySurface* ds,
                                                     uint8_t id) {
        const absl::MutexLock lock(display_access_);
        auto display = std::make_shared<QemuDisplay>(loop_, qemu_loop_, console, ds, id);

        auto [it, inserted] = displays_.insert({id, display});
        if (!inserted) {
            return absl::AlreadyExistsError(
                    absl::StrFormat("Display with id %d already exists.", id));
        }

        auto dims = display->GetDimensions();

        VLOG(1) << "Created display: " << *display;
        if (id != 0) {
            display->SetActive(false);
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
        auto it = displays_.find(display_id);
        if (it == displays_.end()) {
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
        const absl::MutexLock lock(display_access_);
        auto it = displays_.find(display_id);
        if (it == displays_.end()) {
            return absl::NotFoundError(
                    absl::StrFormat("Display: %d does not exist (already removed?).", display_id));
        }
        displays_.erase(it);
        FireEvent({DisplayEvent{DisplayEvent::DeletedEvent{display_id}}});
        return absl::OkStatus();
    }

    absl::Status EraseVirtualDisplay(DisplayId display_id) {
        const absl::MutexLock lock(display_access_);
        auto it = virtual_displays_.find(display_id);
        if (it == virtual_displays_.end()) {
            return absl::NotFoundError(
                    absl::StrFormat("Display: %d does not exist (already removed?).", display_id));
        }
        virtual_displays_.erase(it);
        FireEvent({DisplayEvent{DisplayEvent::DeletedEvent{display_id}}});
        return absl::OkStatus();
    }

    bool IsEnabled() const override { return true; }

    std::vector<DisplayPtr> Displays() const override {
        const absl::MutexLock lock(display_access_);
        std::vector<DisplayPtr> displays;
        for (const auto& pair : displays_) {
            if (pair.second->Active()) {
                displays.push_back(pair.second);
            }
        }
        for (const auto& pair : virtual_displays_) {
            if (pair.second->Active()) {
                displays.push_back(pair.second);
            }
        }

        return displays;
    }

  private:
    mutable absl::Mutex display_access_;
    QemuDisplayMap displays_ ABSL_GUARDED_BY(display_access_);
    VirtualDisplayMap virtual_displays_ ABSL_GUARDED_BY(display_access_);
    EventLoop* qemu_loop_;
};

namespace qemu_multidisplay {
void ConfigureMultiDisplay(EventLoop* loop, EventLoop* qemu_loop) {
    static MultiDisplayImpl instance(loop, qemu_loop);
    IMultiDisplay::InjectSingleton(&instance);
}
}  // namespace qemu_multidisplay

extern "C" void grpc_dpy_gfx_update(struct DisplayChangeListener* dcl, int x, int y, int w, int h) {
    // TODO(jansene): True multidisplay support should go over the qemu consoles, that are tied
    // to gpu0, head:%d
    auto* multi_display = static_cast<MultiDisplayImpl*>(IMultiDisplay::Instance());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multi_display->GetDisplayWeak(index);
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

        display->UpdateSurface(x, y, w, h);
    }
}

extern "C" void grpc_dpy_gfx_refresh(DisplayChangeListener* dcl) {
    // TODO(jansene): Qemu uses this to synchronize the clipboard.
}

extern "C" void grpc_dpy_gfx_switch(struct DisplayChangeListener* dcl,
                                    struct DisplaySurface* new_surface) {
    auto* multi_display = static_cast<MultiDisplayImpl*>(IMultiDisplay::Instance());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        // TODO(whollins): maybe use qemu_console_lookup_by_device_name("gpu0", head, err);
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multi_display->GetDisplayWeak(index);
    if (absl::IsNotFound(device.status())) {
        DisplaySurface* surface = new_surface;
        bool created_surface = false;
        if (index == 1) {
            const auto& hw = ::goldfish::avd_info::GetAvd().Props().hw_config;
            if (hw.hw_sensor_hinge) {
                surface = qemu_create_displaysurface(hw.hw_displayRegion_0_1_width,
                                                     hw.hw_displayRegion_0_1_height);
                created_surface = true;
            }
        }
        auto status = multi_display->CreateDisplayFromQemu(con, surface, index);
        if (status.ok() && created_surface) {
            static_cast<QemuDisplay*>(status.value().lock().get())->SetOwnedSurface(surface);
        }
        return;
    }

    if (auto display = device->lock()) {
        if (new_surface) {
            auto dims = display->GetDimensions();
            if (std::cmp_not_equal(surface_width(new_surface), dims.width) ||
                std::cmp_not_equal(surface_height(new_surface), dims.height)) {
                if (index) {
                    return;
                }
            }
            display->SetOwnedSurface(nullptr);
        }
        display->UpdateSourceImage(new_surface ? new_surface->image : nullptr);
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
            const auto& hw = ::goldfish::avd_info::GetAvd().Props().hw_config;
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
