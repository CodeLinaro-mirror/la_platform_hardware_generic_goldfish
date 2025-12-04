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
#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include "absl/base/call_once.h"
#include "absl/container/flat_hash_map.h"

#include "aemu/base/events/EventSource.h"
#include "android/emulation/control/utils/grpc_event_stream_support.h"
#include "android/misc/GuestStatusDevice.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/event_loop_dispatcher.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/display/MultiDisplay.h"

namespace android {
namespace emulation {
namespace control {

using android::base::eventing::CallbackEventSource;
using android::base::eventing::EventListener;
using ::goldfish::devices::ConnectorRegistry;
using ::goldfish::devices::guest_status::AndroidGuestStatus;
using ::goldfish::display::DisplayEvent;
using ::goldfish::display::DisplayId;
using ::goldfish::display::DisplayPtr;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::ResizeEvent;
using ::goldfish::display::ResizeEventCallbackSource;

// EventChangeSupport which can use optional values for the event.
class NotificationEventChangeSupport : public CallbackEventSource<Notification> {
  public:
    void fireEvent(std::optional<Notification> event) {
        if (event.has_value()) {
            CallbackEventSource<Notification>::fireEvent(*event);
        }
    }
};

// A filtered stream writer that will only write unique notifications
class NotificationStreamWriter : public UniqueEventStreamWriter<Notification> {
  public:
    NotificationStreamWriter(NotificationEventChangeSupport* listener)
            : UniqueEventStreamWriter<Notification>(listener) {}

    // Dispatch an event if it is actually there.
    void eventArrived(const std::optional<Notification> event) {
        if (event.has_value()) {
            UniqueEventStreamWriter<Notification>::eventArrived(*event);
        }
    }
};

// This class implements a factory that can produce a NoticationStreamImpl
// A NotificationStreamImpl is an async grpc implementation for delivering
// notification events.
//
class NotificationStream : public std::enable_shared_from_this<NotificationStream>,
                           public EventListener<DisplayEvent>,
                           public EventListener<ResizeEvent>,
                           public EventListener<AndroidGuestStatus> {
    struct Private {};

  public:
    NotificationStream(IMultiDisplay* display, ConnectorRegistry* connectorRegistry, Private)
            : mMultiDisplay(display), mRegistry(connectorRegistry) {};

    ~NotificationStream();

    // Produce an asynchronous handler for the following gRPC method:
    NotificationStreamWriter* notificationStream();

    void eventArrived(const DisplayEvent& event);
    void eventArrived(const ResizeEvent& event);
    void eventArrived(const AndroidGuestStatus& event);

    static std::shared_ptr<NotificationStream> create(IMultiDisplay* display,
                                                      ConnectorRegistry* connectorRegistry);

  protected:
    NotificationEventChangeSupport mNotificationListeners;

  private:
    void registerListeners();

    std::optional<Notification> getDisplayNotificationEvent();
    std::optional<Notification> getBootedNotificationEvent();

    IMultiDisplay* mMultiDisplay;
    ConnectorRegistry* mRegistry;

    absl::flat_hash_map<DisplayId, DisplayPtr> mDisplays ABSL_GUARDED_BY(mDisplayAccess);
    absl::Mutex mDisplayAccess;

    absl::once_flag mRegisteredDisplayOnceFlag;
    absl::once_flag mRegisteredGuestStatusOnceFlag;
};
}  // namespace control
}  // namespace emulation
}  // namespace android
