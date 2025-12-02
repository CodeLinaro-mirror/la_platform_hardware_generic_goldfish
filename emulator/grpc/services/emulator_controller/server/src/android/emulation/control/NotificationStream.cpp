// Copyright (C) 2023 The Android Open Source Project
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
#include "android/emulation/control/NotificationStream.h"

#include <assert.h>

#include <optional>

#include "absl/log/log.h"

#include "aemu/base/EventNotificationSupport.h"
#include "android/emulation/control/DisplayService.h"
#include "android/misc/GuestStatusDevice.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::devices::guest_status::IGuestStatusDevice;
using ::goldfish::display::ResizeEventCallbackSource;

std::optional<Notification> NotificationStream::getDisplayNotificationEvent() {
    Notification event;
    event.set_event(Notification::DISPLAY_CONFIGURATIONS_CHANGED_UI);

    auto eventDetails = event.mutable_displayconfigurationschangednotification();
    auto displayConfig = eventDetails->mutable_displayconfigurations();

    auto status = DisplayServiceImpl::getDisplayConfigurations(mMultiDisplay, displayConfig);
    if (!status.ok()) {
        LOG(WARNING) << "Failed to retrieve display configuration, ignoring this change due to:"
                     << status.error_code();
        return std::nullopt;
    }

    VLOG(1) << "Display notification event: " << event.ShortDebugString();
    return event;
}

NotificationStream::~NotificationStream() {}

std::optional<Notification> NotificationStream::getBootedNotificationEvent() {
    auto weak = mRegistry->activeDevice<IGuestStatusDevice>();
    if (auto guest_state = weak.lock()) {
        if (guest_state->hasBooted()) {
            Notification event;
            auto eventDetails = event.mutable_booted();
            eventDetails->set_time(guest_state->bootTime()->count());
            return event;
        }
    }
    return std::nullopt;
}

void NotificationStream::eventArrived(const DisplayEvent& event) {
    mNotificationListeners.fireEvent(getDisplayNotificationEvent());
    if (event.isAddedEvent()) {
        if (auto display = event.display().lock()) {
            absl::MutexLock lock(&mDisplayAccess);
            mDisplays[display->id()] = display;
            display->ResizeEventCallbackSource::addListener(shared_from_this());
        }
    }
    if (event.isDeletedEvent()) {
        absl::MutexLock lock(&mDisplayAccess);
        mDisplays.erase(event.displayId());
    }
}

void NotificationStream::eventArrived(const ResizeEvent& event) {
    mNotificationListeners.fireEvent(getDisplayNotificationEvent());
}

void NotificationStream::eventArrived(const AndroidGuestStatus& event) {
    mNotificationListeners.fireEvent(getBootedNotificationEvent());
}

std::shared_ptr<NotificationStream> NotificationStream::create(
        IMultiDisplay* display, ConnectorRegistry* connectorRegistry) {
    return std::make_shared<NotificationStream>(display, connectorRegistry, Private());
}
NotificationStreamWriter* NotificationStream::notificationStream() {
    registerListeners();
    auto stream = new NotificationStreamWriter(&mNotificationListeners);
    stream->eventArrived(getBootedNotificationEvent());
    stream->eventArrived(getDisplayNotificationEvent());
    return stream;
}

void NotificationStream::registerListeners() {
    VLOG(1) << "Registering listeners.";
    absl::call_once(mRegisteredDisplayOnceFlag, [&]() {
        VLOG(1) << "Registering display change listeners.";
        // This gives us add and remove.
        mMultiDisplay->addListener(shared_from_this());
        VLOG(1) << "Done registering display change listeners, obtaining individial displays.";
        // Next we want to register display changes to existing displays.
        for (const auto& weakdisplay : mMultiDisplay->displays()) {
            VLOG(1) << "Attempting to register next display";
            if (auto display = weakdisplay.lock()) {
                // Whelp, someone added us as we are iterating!
                VLOG(1) << "register display: " << display->id();
                absl::MutexLock lock(&mDisplayAccess);
                if (!mDisplays.contains(display->id())) {
                    mDisplays[display->id()] = display;
                    display->ResizeEventCallbackSource::addListener(shared_from_this());
                }
            }
        }
        VLOG(1) << "Done registering display change listeners.";
    });

    auto weak = mRegistry->activeDevice<IGuestStatusDevice>();
    if (auto guest_state = weak.lock()) {
        absl::call_once(mRegisteredGuestStatusOnceFlag, [&]() {
            VLOG(1) << "Registering guest status change listeners.";
            guest_state->addListener(shared_from_this());
        });
    };
}

}  // namespace control
}  // namespace emulation
}  // namespace android
