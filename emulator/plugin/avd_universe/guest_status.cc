// Copyright 2026 The Android Open Source Project
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

#include "goldfish/avd_universe/guest_status/guest_status.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/metrics/metrics_reporter.h"

namespace goldfish::avd_universe::guest_status {

namespace {
/*
 * This magic string MUST be printed: this is how the tools detect
 * that the system image booted.
 *
 * Use `WARNING`, otherwise, logger does no flush and we
 * don't know it boot completes in timely manner.
 */
void NotifyToolsBootcomplete(const int64_t duration_ms) {
    LOG(WARNING) << "Boot completed in " << duration_ms << " ms";
}
}  // namespace

archive::IWriter& operator<<(archive::IWriter& w, const GuestStatus& val) {
    w << val.resetT_ << val.bootcompleteT_ << val.heartbeatCounter_;
    return w;
}

absl::Status ReadValue(archive::IReader& r, GuestStatus& val) {
    return ReadValue(r, val.resetT_, val.bootcompleteT_, val.heartbeatCounter_);
}

GuestStatus::GuestStatus() : resetT_(absl::Now()) {}

bool GuestStatus::IsBootCompleted() const {
    return bootcompleteT_ != absl::UnixEpoch();
}

absl::Duration GuestStatus::GetBootCompleteDuration() const {
    DCHECK(bootcompleteT_ >= resetT_);
    return bootcompleteT_ - resetT_;
}

void GuestStatus::Reset(const absl::Time t) {
    resetT_ = t;
    bootcompleteT_ = absl::UnixEpoch();
    heartbeatCounter_ = 0;
}

void GuestStatus::SetBootComplete(const absl::Time t) {
    if (bootcompleteT_ == absl::UnixEpoch()) {
        /*
         * Avoid sending "bootcomplete" events for `adb reboot`
         * because we don't receive `reset` events for them.
         */
        bootcompleteT_ = t;

        NotifyBootcomplete(bootcompleteT_ - resetT_);
    }
}

void GuestStatus::OnPostLoad() const {
    if (bootcompleteT_ >= resetT_) {
        NotifyBootcomplete(bootcompleteT_ - resetT_);
    }
}

void GuestStatus::NotifyBootcomplete(const absl::Duration duration) const {
    const int64_t duration_ms = absl::ToInt64Milliseconds(duration);
    DCHECK(duration_ms >= 0);

    NotifyToolsBootcomplete(duration_ms);
    if (grpcNotificationSource_) {
        goldfish::avd_universe::grpc::GrpcNotification notification;
        notification.mutable_booted()->set_time(static_cast<int32_t>(duration_ms));

        grpcNotificationSource_->FireEvent(notification);
    }
    if (metrics_reporter_) {
        metrics_reporter_->Report([duration_ms](android_studio::AndroidStudioEvent& event) {
            auto& boot_info = *event.mutable_emulator_details()->mutable_boot_info();
            boot_info.set_boot_status(android_studio::EmulatorBootInfo::BOOT_COMPLETED);
            boot_info.set_duration_ms(duration_ms);
        });
    }
}

}  // namespace goldfish::avd_universe::guest_status
