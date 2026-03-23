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

absl::StatusOr<CellInfo> ModemSimulatorClient::SetCellInfo(const CellInfo&) {
    return absl::UnimplementedError("`SetCellInfo` is not yet implemented.");
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
