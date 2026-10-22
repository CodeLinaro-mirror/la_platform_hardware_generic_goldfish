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

#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "android/emulation/control/grpc_event_stream_support.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "notification_store.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_universe::grpc::GrpcNotification;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;

inline ::grpc::ServerWriteReactor<GrpcNotification>* CreateNotificationStreamWriter(
        GrpcNotificationEventSource* topic, NotificationStore* store) {
    VLOG(1) << "notification stream created";
    std::vector<GrpcNotification> initial =
            store != nullptr ? store->GetLatest() : std::vector<GrpcNotification>{};
    return new UniqueEventStreamWriter<GrpcNotification>(topic, std::move(initial));
}

}  // namespace control
}  // namespace emulation
}  // namespace android
