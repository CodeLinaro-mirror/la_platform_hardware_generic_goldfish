// Copyright (C) 2026 The Android Open Source Project
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

#include "battery_service_impl.h"

#include <cstdint>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/simple_async_grpc.h"

namespace goldfish::grpc::v2 {

using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::v2::BatteryState;
using ::android::emulation::v2::ChargerSource;
using ::android::emulation::v2::GetBatteryStateRequest;
using ::android::emulation::v2::StreamBatteryStateRequest;
using ::android::emulation::v2::UpdateBatteryStateRequest;
using ::goldfish::avd_universe::battery::Battery;
using ::grpc::CallbackServerContext;
using ::grpc::ServerContext;
using ::grpc::ServerWriteReactor;
using ::grpc::Status;
using ::grpc::StatusCode;

namespace {

constexpr int32_t kMinChargePercent = 0;
constexpr int32_t kMaxChargePercent = 100;

Status BatteryUnsupportedStatus() {
    return Status(StatusCode::FAILED_PRECONDITION,
                  "Failed to access battery state: device has no battery subsystem "
                  "(hw.battery=no). Check DeviceCapabilities.battery before calling "
                  "BatteryService.");
}

absl::Status ValidateChargeLevel(int32_t level_percent) {
    if (level_percent < kMinChargePercent || level_percent > kMaxChargePercent) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Failed to update battery state: level_percent must be in range "
                                "[%d, %d], got %d. Pass a valid charge percentage.",
                                kMinChargePercent, kMaxChargePercent, level_percent));
    }
    return absl::OkStatus();
}

absl::StatusOr<Battery::Status> ToBatteryStatus(BatteryState::State state) {
    switch (state) {
    case BatteryState::CHARGING:
        return Battery::Status::kCharging;
    case BatteryState::DISCHARGING:
        return Battery::Status::kDischarging;
    case BatteryState::NOT_CHARGING:
        return Battery::Status::kNotCharging;
    case BatteryState::FULL:
        return Battery::Status::kFull;
    case BatteryState::STATE_UNSPECIFIED:
    default:
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to update battery state: invalid or unspecified state enum value %d. "
                "Pass CHARGING, DISCHARGING, NOT_CHARGING, or FULL, or omit state to leave it "
                "unchanged.",
                static_cast<int>(state)));
    }
}

absl::StatusOr<Battery::Charger> ToBatteryCharger(ChargerSource charger) {
    switch (charger) {
    case ChargerSource::CHARGER_SOURCE_NONE:
        return Battery::Charger::kNone;
    case ChargerSource::CHARGER_SOURCE_AC:
        return Battery::Charger::kAc;
    case ChargerSource::CHARGER_SOURCE_USB:
    case ChargerSource::CHARGER_SOURCE_WIRELESS:
    case ChargerSource::CHARGER_SOURCE_DOCK:
        return absl::FailedPreconditionError(absl::StrFormat(
                "Failed to update battery state: charger %s (%d) is not supported by "
                "goldfish_battery hardware. Pass CHARGER_SOURCE_NONE or a source advertised in "
                "BatteryCapabilities.supported_chargers (CHARGER_SOURCE_AC).",
                ChargerSource_Name(charger), static_cast<int>(charger)));
    case ChargerSource::CHARGER_SOURCE_UNSPECIFIED:
    default:
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to update battery state: invalid or unspecified charger enum value %d. "
                "Pass CHARGER_SOURCE_NONE or CHARGER_SOURCE_AC, or omit charger to leave it "
                "unchanged.",
                static_cast<int>(charger)));
    }
}

absl::StatusOr<Battery::Health> ToBatteryHealth(BatteryState::Health health) {
    switch (health) {
    case BatteryState::GOOD:
        return Battery::Health::kGood;
    case BatteryState::OVERHEAT:
        return Battery::Health::kOverheated;
    case BatteryState::DEAD:
        return Battery::Health::kDead;
    case BatteryState::OVER_VOLTAGE:
        return Battery::Health::kOvervoltage;
    case BatteryState::COLD:
        return absl::FailedPreconditionError(absl::StrFormat(
                "Failed to update battery state: health COLD (%d) is not supported by "
                "goldfish_battery hardware. Pass GOOD, OVERHEAT, DEAD, or OVER_VOLTAGE.",
                static_cast<int>(health)));
    case BatteryState::HEALTH_UNSPECIFIED:
    default:
        return absl::InvalidArgumentError(absl::StrFormat(
                "Failed to update battery state: invalid or unspecified health enum value %d. "
                "Pass GOOD, OVERHEAT, DEAD, or OVER_VOLTAGE, or omit health to leave it "
                "unchanged.",
                static_cast<int>(health)));
    }
}

}  // namespace

BatteryServiceImpl::BatteryServiceImpl(ObservableBattery& observable_battery, Options options)
        : observable_battery_(observable_battery), options_(options) {
    OnBatteryUpdated(observable_battery_.GetValue());
    battery_callback_id_ = observable_battery_.AddCallback(
            [this](const Battery& state) { OnBatteryUpdated(state); });
}

BatteryServiceImpl::~BatteryServiceImpl() {
    if (battery_callback_id_ != ObservableBattery::kInvalidCallbackId) {
        observable_battery_.RemoveCallback(battery_callback_id_);
    }
}

void BatteryServiceImpl::PopulateBatteryState(BatteryState* state) const {
    absl::MutexLock lock(&battery_mutex_);
    *state = current_battery_;
}

void BatteryServiceImpl::OnBatteryUpdated(const Battery& state) {
    {
        absl::MutexLock lock(&battery_mutex_);
        PopulateBatteryProto(state, &current_battery_);
    }
    battery_events_.FireEvent(state);
}

void BatteryServiceImpl::PopulateBatteryProto(const Battery& in, BatteryState* out) {
    out->set_level_percent(in.charge_level);

    switch (in.status) {
    case Battery::Status::kCharging:
        out->set_state(BatteryState::CHARGING);
        break;
    case Battery::Status::kDischarging:
        out->set_state(BatteryState::DISCHARGING);
        break;
    case Battery::Status::kNotCharging:
        out->set_state(BatteryState::NOT_CHARGING);
        break;
    case Battery::Status::kFull:
        out->set_state(BatteryState::FULL);
        break;
    case Battery::Status::kUnknown:
    default:
        out->set_state(BatteryState::STATE_UNSPECIFIED);
        break;
    }

    switch (in.charger) {
    case Battery::Charger::kNone:
        out->set_charger(ChargerSource::CHARGER_SOURCE_NONE);
        break;
    case Battery::Charger::kAc:
        out->set_charger(ChargerSource::CHARGER_SOURCE_AC);
        break;
    case Battery::Charger::kUsb:
        out->set_charger(ChargerSource::CHARGER_SOURCE_USB);
        break;
    case Battery::Charger::kWireless:
        out->set_charger(ChargerSource::CHARGER_SOURCE_WIRELESS);
        break;
    default:
        out->set_charger(ChargerSource::CHARGER_SOURCE_UNSPECIFIED);
        break;
    }

    switch (in.health) {
    case Battery::Health::kGood:
        out->set_health(BatteryState::GOOD);
        break;
    case Battery::Health::kOverheated:
        out->set_health(BatteryState::OVERHEAT);
        break;
    case Battery::Health::kDead:
        out->set_health(BatteryState::DEAD);
        break;
    case Battery::Health::kOvervoltage:
        out->set_health(BatteryState::OVER_VOLTAGE);
        break;
    case Battery::Health::kFailed:
    default:
        out->set_health(BatteryState::HEALTH_UNSPECIFIED);
        break;
    }
}

Status BatteryServiceImpl::GetBatteryState(ServerContext* /*context*/,
                                           const GetBatteryStateRequest* /*request*/,
                                           BatteryState* reply) {
    if (!options_.has_battery) {
        return BatteryUnsupportedStatus();
    }
    PopulateBatteryState(reply);
    return Status::OK;
}

ServerWriteReactor<BatteryState>* BatteryServiceImpl::StreamBatteryState(
        CallbackServerContext* /*context*/, const StreamBatteryStateRequest* /*request*/) {
    if (!options_.has_battery) {
        return new ::ErrorServerWriter<BatteryState>(BatteryUnsupportedStatus());
    }
    return CreateBatteryStateWriter();
}

Status BatteryServiceImpl::UpdateBatteryState(ServerContext* /*context*/,
                                              const UpdateBatteryStateRequest* request,
                                              BatteryState* reply) {
    if (!options_.has_battery) {
        return BatteryUnsupportedStatus();
    }
    if (options_.read_only) {
        return Status(StatusCode::PERMISSION_DENIED,
                      "Failed to update battery state: battery state mutations are disabled on "
                      "this device (ACCESS_MODE_READ_ONLY). Query GetBatteryState instead.");
    }

    std::optional<int32_t> level_percent;
    if (request->has_level_percent()) {
        if (auto status = ValidateChargeLevel(request->level_percent()); !status.ok()) {
            return AbslStatusToGrpcStatus(status);
        }
        level_percent = request->level_percent();
    }

    std::optional<Battery::Status> status;
    if (request->has_state()) {
        auto parsed_status = ToBatteryStatus(request->state());
        if (!parsed_status.ok()) {
            return AbslStatusToGrpcStatus(parsed_status.status());
        }
        status = *parsed_status;
    }

    std::optional<Battery::Charger> charger;
    if (request->has_charger()) {
        auto parsed_charger = ToBatteryCharger(request->charger());
        if (!parsed_charger.ok()) {
            return AbslStatusToGrpcStatus(parsed_charger.status());
        }
        charger = *parsed_charger;
    }

    std::optional<Battery::Health> health;
    if (request->has_health()) {
        auto parsed_health = ToBatteryHealth(request->health());
        if (!parsed_health.ok()) {
            return AbslStatusToGrpcStatus(parsed_health.status());
        }
        health = *parsed_health;
    }

    Battery updated;
    {
        absl::MutexLock lock(&update_mutex_);
        updated = observable_battery_.GetValue();
        if (level_percent.has_value()) {
            updated.charge_level = *level_percent;
        }
        if (status.has_value()) {
            updated.status = *status;
        }
        if (charger.has_value()) {
            updated.charger = *charger;
        }
        if (health.has_value()) {
            updated.health = *health;
        }
        observable_battery_.SetValue(updated);
    }

    PopulateBatteryProto(updated, reply);
    return Status::OK;
}

}  // namespace goldfish::grpc::v2
