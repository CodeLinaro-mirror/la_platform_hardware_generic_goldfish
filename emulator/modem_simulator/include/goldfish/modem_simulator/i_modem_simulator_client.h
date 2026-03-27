/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace goldfish::modem_simulator {

struct IModemSimulatorClient {
    enum class SignalStrength : uint8_t {
        NONE_OR_UNKNOWN = 0,
        POOR,
        MODERATE,
        GOOD,
        GREAT,
    };

    struct CellIdentity {
        std::string operatorLong;
        std::string operatorShort;
    };

    enum class CellStandard : uint8_t {
        UNKNOWN = 0,
        GSM = 1,
        HSCSD = 2,
        GPRS = 3,
        EDGE = 4,
        UMTS = 5,
        HSDPA = 6,
        LTE = 7,
        FULL = 8,
        NR_5G = 9,
    };

    enum class CellStatus : uint8_t {
        UNKNOWN = 0,
        HOME = 1,
        ROAMING = 2,
        SEARCHING = 3,
        DENIED = 4,
        UNREGISTERED = 5,
    };

    enum class MeterStatus : uint8_t {
        UNKNOWN = 0,
        METERED = 1,
        TEMPORARILY_NOT_METERED = 2,
    };

    enum class SimStatus : uint8_t {
        UNKNOWN = 0,
        NOT_PRESENT = 1,
        PRESENT = 2,
    };

    enum class CallStatus : uint8_t {
        OK = 0,
        NUMBER_NOT_FOUND = 1,
        EXCEED_MAX_NUM = 2,
        RADIO_OFF = 3,
    };

    enum class CallState : uint8_t {
        UNSPECIFIED = 0,
        ACTIVE = 1,
        HELD = 2,
        DIALING = 3,
        ALERTING = 4,
        INCOMING = 5,
        WAITING = 6,
    };

    enum class CallDirection : uint8_t {
        UNSPECIFIED = 0,
        OUTBOUND = 1,
        INBOUND = 2,
    };

    struct CellInfo {
        CellIdentity identity;  // unsupported by UI
        CellStandard standard = CellStandard::UNKNOWN;
        CellStatus voiceStatus = CellStatus::UNKNOWN;
        CellStatus dataStatus = CellStatus::UNKNOWN;  // unsupported by modem
        SignalStrength signalStrength = SignalStrength::NONE_OR_UNKNOWN;
        MeterStatus meterStatus = MeterStatus::UNKNOWN;  // unsupported by modem
        SimStatus simStatus = SimStatus::UNKNOWN;        // unsupported by modem
    };

    struct Call {
        std::string number;
        CallDirection direction = CallDirection::UNSPECIFIED;
        CallState state = CallState::UNSPECIFIED;
    };

    virtual ~IModemSimulatorClient() = default;

    virtual absl::StatusOr<CellInfo> SetCellInfo(const CellInfo&) = 0;
    virtual absl::StatusOr<CellInfo> GetCellInfo() = 0;
    virtual absl::StatusOr<Call> CreateCall(const Call&) = 0;
    virtual absl::StatusOr<Call> UpdateCall(const Call&) = 0;
    virtual absl::Status DeleteCall(const Call&) = 0;
    virtual absl::StatusOr<std::vector<Call>> ListCalls() = 0;
    virtual absl::Status ReceiveSmsUtf8(std::string_view sender, std::string_view message) = 0;
    virtual absl::Status ReceiveSmsEncoded(std::vector<uint8_t> binary) = 0;
    virtual absl::Status UpdateClock() = 0;
};

}  // namespace goldfish::modem_simulator
