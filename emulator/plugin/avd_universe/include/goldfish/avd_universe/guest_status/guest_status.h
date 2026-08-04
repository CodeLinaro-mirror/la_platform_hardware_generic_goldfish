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

#include <cstdint>

#include "goldfish/archive/time.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "goldfish/metrics/metrics_reporter.h"

namespace goldfish::avd_universe::guest_status {

struct GuestStatus {
    GuestStatus();

    bool IsBootCompleted() const;
    absl::Time GetResetT() const { return resetT_; }
    absl::Time GetBootCompleteT() const { return bootcompleteT_; }
    absl::Duration GetBootCompleteDuration() const;
    uint64_t GetHeartbeatCounter() const { return heartbeatCounter_; }

    void SetMetricsReporter(::goldfish::metrics::MetricsReporter* metrics_reporter) {
        metrics_reporter_ = metrics_reporter;
    }
    void Reset(absl::Time);
    void SetBootComplete(absl::Time);
    void Heartbeat() { ++heartbeatCounter_; }

    void OnPostLoad() const;

    void SetGrpcNotificationChannel(avd_universe::grpc::GrpcNotificationEventSource* src) {
        grpcNotificationSource_ = src;
    }

    friend archive::IWriter& operator<<(archive::IWriter&, const GuestStatus&);
    friend absl::Status ReadValue(archive::IReader&, GuestStatus&);

  private:
    void NotifyBootcomplete(absl::Duration) const;

    ::goldfish::metrics::MetricsReporter* metrics_reporter_ = nullptr;
    absl::Time resetT_;
    absl::Time bootcompleteT_;
    uint64_t heartbeatCounter_ = 0;
    avd_universe::grpc::GrpcNotificationEventSource* grpcNotificationSource_ = nullptr;
};

}  // namespace goldfish::avd_universe::guest_status
