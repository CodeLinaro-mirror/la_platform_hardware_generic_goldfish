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

#include "absl/log/log.h"
#include "android/emulation/control/grpc_event_stream_support.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "notification_store.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_universe::grpc::GrpcNotification;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;

class NotificationStreamWriter : public UniqueEventStreamWriter<GrpcNotification> {
  public:
    NotificationStreamWriter(GrpcNotificationEventSource* topic, NotificationStore* store)
            : UniqueEventStreamWriter<GrpcNotification>(topic) {
        VLOG(1) << "notification stream created";
        if (store) {
            for (const auto& notification : store->GetLatest()) {
                Write(notification);
            }
        }
    }

    // Dispatch an event if it is actually there.
    void EventArrived(const GrpcNotification &event) override {
        VLOG(2) << "EVENT BEING SENT: " << event.ShortDebugString();
        UniqueEventStreamWriter<GrpcNotification>::EventArrived(std::move(event));
    }
};

}  // namespace control
}  // namespace emulation
}  // namespace android
