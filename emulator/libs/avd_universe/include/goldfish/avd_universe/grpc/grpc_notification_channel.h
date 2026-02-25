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
#pragma once

#include "emulator_controller.grpc.pb.h"
#include "goldfish/eventing/event_sources.h"

namespace goldfish::avd_universe::grpc {

using GrpcNotification = android::emulation::control::Notification;

struct GrpcNotificationEventSource
        : public android::base::eventing::CallbackEventSource<GrpcNotification> {
    virtual ~GrpcNotificationEventSource() = default;
    virtual void FireEvent(const GrpcNotification& event) {
        android::base::eventing::CallbackEventSource<GrpcNotification>::FireEvent(event);
    }
};

}  // namespace goldfish::avd_universe::grpc
