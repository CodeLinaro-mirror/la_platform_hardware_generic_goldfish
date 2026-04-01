//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/modem_simulator/modem_simulator_client.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "android/sockets/scoped_socket.h"
#include "android/sockets/socket_utils.h"
#include "goldfish/gsm/sms.h"
#include "goldfish/parsing/hexbin.h"

namespace goldfish::modem_simulator {
using ::android::base::ScopedSocket;
using ::android::base::socketSendAll;

using ::goldfish::gsm::SmsPdu;
using ::goldfish::gsm::SmsPdusFromBinary;
using ::goldfish::gsm::SmsPdusFromUtf8;
using ::goldfish::parsing::BinToHex;

using Call = IModemSimulatorClient::Call;
using CellInfo = IModemSimulatorClient::CellInfo;

namespace {
unsigned ToInt(const IModemSimulatorClient::SignalStrength ss) {
    switch (ss) {
    case IModemSimulatorClient::SignalStrength::NONE_OR_UNKNOWN:
        return 0;

    case IModemSimulatorClient::SignalStrength::POOR:
        return 1;

    case IModemSimulatorClient::SignalStrength::MODERATE:
        return 2;

    case IModemSimulatorClient::SignalStrength::GOOD:
        return 3;

    case IModemSimulatorClient::SignalStrength::GREAT:
        return 4;
    }

    return 0;
}

unsigned ToInt(const IModemSimulatorClient::CellStandard cs) {
    switch (cs) {  // see network_service.h
    case IModemSimulatorClient::CellStandard::UNKNOWN:
    case IModemSimulatorClient::CellStandard::GSM:
    case IModemSimulatorClient::CellStandard::HSCSD:
    case IModemSimulatorClient::CellStandard::GPRS:
    case IModemSimulatorClient::CellStandard::EDGE:
        return 1U << 0;  // GSM

    case IModemSimulatorClient::CellStandard::UMTS:
    case IModemSimulatorClient::CellStandard::HSDPA:
        return 1U << 1;  // WCDMA

    case IModemSimulatorClient::CellStandard::LTE:
        return 1U << 5;  // LTE

    case IModemSimulatorClient::CellStandard::FULL:
    case IModemSimulatorClient::CellStandard::NR_5G:
        return 1U << 6;  // 5G
    }

    return 1U << 0;  // GSM
}

unsigned ToInt(const IModemSimulatorClient::CellStatus cs) {
    switch (cs) {
    case IModemSimulatorClient::CellStatus::UNKNOWN:
        return 0;

    case IModemSimulatorClient::CellStatus::HOME:
        return 1;

    case IModemSimulatorClient::CellStatus::ROAMING:
        return 2;

    case IModemSimulatorClient::CellStatus::SEARCHING:
        return 3;

    case IModemSimulatorClient::CellStatus::DENIED:
        return 4;

    case IModemSimulatorClient::CellStatus::UNREGISTERED:
        return 5;
    }

    return 0;
}

absl::StatusOr<ScopedSocket> ConnectToSimulator(const int serverPort) {
    ScopedSocket fd(android::base::socketTcp4LoopbackClient(serverPort));
    if (!fd.valid()) {
        fd.reset(android::base::socketTcp6LoopbackClient(serverPort));
    }

    if (!fd.valid()) {
        return absl::UnavailableError(absl::StrCat("Failed to connect to modem simulator on port ",
                                                   serverPort, " (tried IPv4 and IPv6)"));
    }

    // Send the "REM0" registration sequence to attach as a remote client
    if (!socketSendAll(fd.get(), "REM0", 4)) {
        return absl::InternalError("Failed to send `REM0` handshake to modem simulator");
    }

    return fd;
}

absl::Status SetSignalStrength(ScopedSocket& socket,
                               const IModemSimulatorClient::SignalStrength ss) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTESIGNAL: "sv, ToInt(ss), "\r"sv);

    if (!socketSendAll(socket.get(), req.data(), req.size())) {
        return absl::InternalError("Failed to send AT command");
    }

    return absl::OkStatus();
}

absl::Status SetCellStandard(ScopedSocket& socket, const IModemSimulatorClient::CellStandard cs) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTECTEC: "sv, ToInt(cs), "\r"sv);

    if (!socketSendAll(socket.get(), req.data(), req.size())) {
        return absl::InternalError("Failed to send AT command");
    }

    return absl::OkStatus();
}

absl::Status SetVoiceStatus(ScopedSocket& socket, const IModemSimulatorClient::CellStatus cs) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTEREG: "sv, ToInt(cs), "\r"sv);

    if (!socketSendAll(socket.get(), req.data(), req.size())) {
        return absl::InternalError("Failed to send AT command");
    }

    return absl::OkStatus();
}

absl::Status SendSmsPdus(const int serverPort, const std::vector<SmsPdu>& pdus) {
    const absl::StatusOr<ScopedSocket> socket = ConnectToSimulator(serverPort);
    if (!socket.ok()) {
        return socket.status();
    }

    for (const SmsPdu& pdu : pdus) {
        using namespace std::literals::string_view_literals;
        const std::string req = absl::StrCat("AT+REMOTESMS="sv, BinToHex(pdu.data), "\r"sv);

        if (!socketSendAll(socket->get(), req.data(), req.size())) {
            return absl::InternalError("Failed to send AT command");
        }
    }

    return absl::OkStatus();
}

}  // namespace

ModemSimulatorClient::ModemSimulatorClient(int serverPort) : serverPort_(serverPort) {}

absl::StatusOr<CellInfo> ModemSimulatorClient::SetCellInfo(const CellInfo& ci) {
    absl::StatusOr<ScopedSocket> socket = ConnectToSimulator(serverPort_);
    if (!socket.ok()) {
        return socket.status();
    }

    if (const auto status = SetCellStandard(*socket, ci.standard); !status.ok()) {
        return status;
    }

    if (const auto status = SetVoiceStatus(*socket, ci.voiceStatus); !status.ok()) {
        return status;
    }

    if (const auto status = SetSignalStrength(*socket, ci.signalStrength); !status.ok()) {
        return status;
    }

    return ci;
}

absl::StatusOr<CellInfo> ModemSimulatorClient::GetCellInfo() {
    return absl::UnimplementedError("`GetCellInfo` is not yet implemented.");
}

absl::StatusOr<Call> ModemSimulatorClient::CreateCall(const Call&) {
    return absl::UnimplementedError("`CreateCall` is not yet implemented.");
}

absl::StatusOr<Call> ModemSimulatorClient::UpdateCall(const Call&) {
    return absl::UnimplementedError("`UpdateCall` is not yet implemented.");
}

absl::Status ModemSimulatorClient::DeleteCall(const Call&) {
    return absl::UnimplementedError("`DeleteCall` is not yet implemented.");
}

absl::StatusOr<std::vector<Call>> ModemSimulatorClient::ListCalls() {
    return absl::UnimplementedError("`ListCalls` is not yet implemented.");
}

absl::Status ModemSimulatorClient::ReceiveSmsUtf8(const std::string_view sender,
                                                  const std::string_view message) {
    const absl::StatusOr<std::vector<SmsPdu>> pdus = SmsPdusFromUtf8(sender, message);
    if (!pdus.ok()) {
        return pdus.status();
    }

    return SendSmsPdus(serverPort_, *pdus);
}

absl::Status ModemSimulatorClient::ReceiveSmsEncoded(std::vector<uint8_t> binary) {
    const absl::StatusOr<std::vector<SmsPdu>> pdus = SmsPdusFromBinary(std::move(binary));
    if (!pdus.ok()) {
        return pdus.status();
    }

    return SendSmsPdus(serverPort_, *pdus);
}

absl::Status ModemSimulatorClient::UpdateClock() {
    const absl::StatusOr<ScopedSocket> socket = ConnectToSimulator(serverPort_);
    if (!socket.ok()) {
        return socket.status();
    }

    using namespace std::literals::string_view_literals;
    const std::string_view kCommand = "AT+REMOTETIMEUPDATE: \r"sv;

    if (!android::base::socketSendAll(socket->get(), kCommand.data(), kCommand.size())) {
        return absl::InternalError("Failed to send AT command");
    }

    return absl::OkStatus();
}

}  // namespace goldfish::modem_simulator
