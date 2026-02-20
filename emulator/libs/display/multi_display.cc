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

#include <misc.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include "emulator/libs/display/QemuDisplay.h"
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

class MultiDisplayImpl : public IMultiDisplay {
  public:
    MultiDisplayImpl(EventLoop* loop, EventLoop* qemu_loop)
            : IMultiDisplay(loop), qemu_loop_(qemu_loop) {}
    ~MultiDisplayImpl() override = default;

    absl::StatusOr<DisplayPtr> CreateDisplay(DisplayId display_id, uint32_t width, uint32_t height,
                                             uint32_t dpi, uint32_t flags) override {
        const absl::MutexLock lock(&display_access_);
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
        const absl::MutexLock lock(&display_access_);
        auto display = std::make_shared<QemuDisplay>(loop_, qemu_loop_, console, ds, id);

        auto [it, inserted] = displays_.insert({id, display});
        if (!inserted) {
            return absl::AlreadyExistsError(
                    absl::StrFormat("Display with id %d already exists.", id));
        }

        VLOG(1) << "Created display: " << *display;
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
        const absl::MutexLock lock(&display_access_);
        auto it = virtual_displays_.find(display_id);
        if (it == virtual_displays_.end()) {
            return absl::NotFoundError(absl::StrFormat("Invalid display: %d", display_id));
        }
        return it->second;
    }

    absl::StatusOr<WeakDisplayImpl> GetDisplayWeak(DisplayId display_id) const {
        const absl::MutexLock lock(&display_access_);
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
        const absl::MutexLock lock(&display_access_);
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
        const absl::MutexLock lock(&display_access_);
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
        const absl::MutexLock lock(&display_access_);
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
        LOG(INFO) << "grpc_dpy_gfx_update: Console is NULL, using default";
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multi_display->GetDisplayWeak(index);
    if (!device.ok()) {
        LOG_EVERY_N(ERROR, 60) << "Unable to find a display to handle gfx changes: "
                               << device.status();
        return;
    }

    if (auto display = device->lock()) {
        display->UpdateSurface(x, y, w, h);
    } else {
        LOG_EVERY_N(ERROR, 60) << "Display with " << index << " is no longer active.";
    }
}

extern "C" void grpc_dpy_gfz_refresh(DisplayChangeListener* dcl) {
    // TODO(jansene): Qemu uses this to synchronize the clipboard.
}

extern "C" void grpc_dpy_gfx_switch(struct DisplayChangeListener* dcl,
                                    struct DisplaySurface* new_surface) {
    auto* multi_display = static_cast<MultiDisplayImpl*>(IMultiDisplay::Instance());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        LOG(INFO) << "grpc_dpy_gfx_switch: Console is NULL, using default";
        // TODO(whollins): maybe use qemu_console_lookup_by_device_name("gpu0", head, err);
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multi_display->GetDisplayWeak(index);
    if (absl::IsNotFound(device.status())) {
        auto status = multi_display->CreateDisplayFromQemu(con, new_surface, index);
        LOG(INFO) << "Display creation state: " << status.status();
        return;
    }

    if (auto display = device->lock()) {
        display->UpdateSourceImage(new_surface->image);
    }
}

}  // namespace goldfish::display
