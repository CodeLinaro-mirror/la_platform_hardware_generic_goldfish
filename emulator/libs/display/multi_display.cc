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

using ::goldfish::physics::Rotation;
using DeviceSkinRotation = ::goldfish::physics::SkinRotation;

namespace goldfish::display {

using SharedDisplayImpl = std::shared_ptr<QemuDisplay>;
using WeakDisplayImpl = std::weak_ptr<QemuDisplay>;
using QemuDisplayMap = std::unordered_map<unsigned, SharedDisplayImpl>;

class MultiDisplayImpl : public IMultiDisplay {
  public:
    MultiDisplayImpl(EventLoop* loop, EventLoop* qemu_loop)
            : IMultiDisplay(loop), mQemuLoop(qemu_loop) {}
    virtual ~MultiDisplayImpl() = default;

    absl::StatusOr<DisplayPtr> createDisplay(DisplayId displayId, uint32_t width,
                                             uint32_t height) override {
        // TODO(jansene): Port multidisplay creation.
        return absl::UnimplementedError("MultiDisplayImpl::createDisplay is not yet supported.");
    }

    absl::StatusOr<DisplayPtr> createDisplayFromQemu(QemuConsole* console, DisplaySurface* ds,
                                                     uint8_t id) {
        absl::MutexLock lock(&mDisplayAccess);
        auto display = std::make_shared<QemuDisplay>(mLoop, mQemuLoop, console, ds, id);

        auto [it, inserted] = mDisplays.insert({id, display});
        if (!inserted) {
            return absl::AlreadyExistsError(
                    absl::StrFormat("Display with id %d already exists.", id));
        }

        VLOG(1) << "Created display: " << *display;
        fireEvent(DisplayEvent{DisplayEvent::AddedEvent{display}});
        return display;
    }

    absl::StatusOr<DisplayPtr> getDisplay(DisplayId displayId) const override {
        auto display = getDisplayWeak(displayId);
        return display;
    }

    absl::StatusOr<WeakDisplayImpl> getDisplayWeak(DisplayId displayId) const {
        absl::MutexLock lock(&mDisplayAccess);
        auto it = mDisplays.find(displayId);
        if (it == mDisplays.end()) {
            return absl::NotFoundError(absl::StrFormat("Invalid display: %d", displayId));
        }
        return it->second;
    }

    absl::Status eraseDisplay(DisplayId displayId) override {
        absl::MutexLock lock(&mDisplayAccess);
        auto it = mDisplays.find(displayId);
        if (it == mDisplays.end()) {
            return absl::NotFoundError(
                    absl::StrFormat("Display: %d does not exist (already removed?).", displayId));
        }
        mDisplays.erase(it);
        fireEvent({DisplayEvent{DisplayEvent::DeletedEvent{displayId}}});
        return absl::OkStatus();
    }

    bool isEnabled() const override {
        // TODO(jansene): Implement true multidisplay support
        return false;
    }

    std::vector<DisplayPtr> displays() const override {
        absl::MutexLock lock(&mDisplayAccess);
        std::vector<DisplayPtr> displays;
        for (const auto& pair : mDisplays) {
            if (pair.second->active()) {
                displays.push_back(pair.second);
            }
        }

        return displays;
    }

  private:
    mutable absl::Mutex mDisplayAccess;
    QemuDisplayMap mDisplays ABSL_GUARDED_BY(mDisplayAccess);
    EventLoop* mQemuLoop;

    static std::unique_ptr<MultiDisplayImpl> gMultidisplay;
};

std::atomic<IMultiDisplay*> IMultiDisplay::gSingleton = nullptr;

IMultiDisplay* IMultiDisplay::instance() {
    return IMultiDisplay::gSingleton;
}

void IMultiDisplay::injectSingleton(IMultiDisplay* display) {
    IMultiDisplay::gSingleton = display;
}

namespace QemuMultidisplay {
void configureMultiDisplay(EventLoop* loop, EventLoop* qemu_loop) {
    static MultiDisplayImpl instance(loop, qemu_loop);
    IMultiDisplay::injectSingleton(&instance);
}
}  // namespace QemuMultidisplay

extern "C" void grpc_dpy_gfx_update(struct DisplayChangeListener* dcl, int x, int y, int w, int h) {
    // TODO(jansene): True multidisplay support should go over the qemu consoles, that are tied
    // to gpu0, head:%d
    auto multiDisplay = static_cast<MultiDisplayImpl*>(IMultiDisplay::instance());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        LOG(INFO) << "grpc_dpy_gfx_update: Console is NULL, using default";
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multiDisplay->getDisplayWeak(index);
    if (!device.ok()) {
        LOG_EVERY_N(ERROR, 60) << "Unable to find a display to handle gfx changes: "
                               << device.status();
        return;
    }

    if (auto display = device->lock()) {
        display->updateSurface(x, y, w, h);
    } else {
        LOG_EVERY_N(ERROR, 60) << "Display with " << index << " is no longer active.";
    }
}

extern "C" void grpc_dpy_gfz_refresh(DisplayChangeListener* dcl) {
    // TODO(jansene): Qemu uses this to synchronize the clipboard.
}

extern "C" void grpc_dpy_gfx_switch(struct DisplayChangeListener* dcl,
                                    struct DisplaySurface* new_surface) {
    auto multiDisplay = static_cast<MultiDisplayImpl*>(IMultiDisplay::instance());
    QemuConsole* con = dcl->con;
    if (con == nullptr) {
        LOG(INFO) << "grpc_dpy_gfx_switch: Console is NULL, using default";
        // TODO(whollins): maybe use qemu_console_lookup_by_device_name("gpu0", head, err);
        con = qemu_console_lookup_default();
    }
    auto index = qemu_console_get_index(con);
    auto device = multiDisplay->getDisplayWeak(index);
    if (absl::IsNotFound(device.status())) {
        auto status = multiDisplay->createDisplayFromQemu(con, new_surface, index);
        LOG(INFO) << "Display creation state: " << status.status();
        return;
    }

    if (auto display = device->lock()) {
        display->updateSourceImage(new_surface->image);
    }
}

}  // namespace goldfish::display
