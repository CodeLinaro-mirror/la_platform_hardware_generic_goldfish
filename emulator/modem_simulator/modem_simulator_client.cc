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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"
#include "goldfish/gsm/sms.h"
#include "goldfish/parsing/hexbin.h"

namespace goldfish::modem_simulator {
using cuttlefish::SharedFD;
using cuttlefish::WriteAll;

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

ModemSimulatorClient::ModemCallState ToModemCallState(const IModemSimulatorClient::CallState cs) {
    switch (cs) {
    case IModemSimulatorClient::CallState::UNSPECIFIED:
        return ModemSimulatorClient::ModemCallState::HANGUP;
    case IModemSimulatorClient::CallState::ACTIVE:
        return ModemSimulatorClient::ModemCallState::ACTIVE;
    case IModemSimulatorClient::CallState::HELD:
        return ModemSimulatorClient::ModemCallState::HELD;
    case IModemSimulatorClient::CallState::DIALING:
        return ModemSimulatorClient::ModemCallState::DIALING;
    case IModemSimulatorClient::CallState::ALERTING:
        return ModemSimulatorClient::ModemCallState::ALERTING;
    case IModemSimulatorClient::CallState::INCOMING:
        return ModemSimulatorClient::ModemCallState::INCOMING;
    case IModemSimulatorClient::CallState::WAITING:
        return ModemSimulatorClient::ModemCallState::WAITING;
    }

    return ModemSimulatorClient::ModemCallState::HANGUP;
}

IModemSimulatorClient::CallState ToCallState(const ModemSimulatorClient::ModemCallState mcs) {
    switch (mcs) {
    case ModemSimulatorClient::ModemCallState::ACTIVE:
        return IModemSimulatorClient::CallState::ACTIVE;
    case ModemSimulatorClient::ModemCallState::HELD:
        return IModemSimulatorClient::CallState::HELD;
    case ModemSimulatorClient::ModemCallState::DIALING:
        return IModemSimulatorClient::CallState::DIALING;
    case ModemSimulatorClient::ModemCallState::ALERTING:
        return IModemSimulatorClient::CallState::ALERTING;
    case ModemSimulatorClient::ModemCallState::INCOMING:
        return IModemSimulatorClient::CallState::INCOMING;
    case ModemSimulatorClient::ModemCallState::WAITING:
        return IModemSimulatorClient::CallState::WAITING;
    case ModemSimulatorClient::ModemCallState::HANGUP:
        return IModemSimulatorClient::CallState::UNSPECIFIED;
    }

    return IModemSimulatorClient::CallState::UNSPECIFIED;
}

absl::Status SendModemRequest(SharedFD& socket, const std::string_view req) {
    VLOG(2) << "Sending modem request:" << req;
    if (WriteAll(socket, req) != req.size()) {
        using namespace std::literals::string_view_literals;
        return absl::InternalError(
                absl::StrCat("Failed to send `"sv, req, "` to the modem simulator"sv));
    } else {
        return absl::OkStatus();
    }
}

absl::StatusOr<SharedFD> ConnectToSimulator(const int serverPort) {
    SharedFD socket = SharedFD::SocketLocalClient(serverPort);
    if (!socket) {
        return absl::UnavailableError(absl::StrCat("Failed to connect to modem simulator on port ",
                                                   serverPort, " (tried IPv4 and IPv6)"));
    }

    using namespace std::literals::string_view_literals;

    // Send the "REM0" registration sequence to attach as a remote client
    absl::Status status = SendModemRequest(socket, "REM0"sv);
    if (!status.ok()) {
        return status;
    }

    return socket;
}

absl::Status SetSignalStrength(SharedFD& socket, const IModemSimulatorClient::SignalStrength ss) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTESIGNAL: "sv, ToInt(ss), "\r"sv);
    return SendModemRequest(socket, req);
}

absl::Status SetCellStandard(SharedFD& socket, const IModemSimulatorClient::CellStandard cs) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTECTEC: "sv, ToInt(cs), "\r"sv);
    return SendModemRequest(socket, req);
}

absl::Status SetVoiceStatus(SharedFD& socket, const IModemSimulatorClient::CellStatus cs) {
    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTEREG: "sv, ToInt(cs), "\r"sv);
    return SendModemRequest(socket, req);
}

absl::Status SendSmsPdus(const int serverPort, const std::vector<SmsPdu>& pdus) {
    absl::StatusOr<SharedFD> socket = ConnectToSimulator(serverPort);
    if (!socket.ok()) {
        return socket.status();
    }

    for (const SmsPdu& pdu : pdus) {
        using namespace std::literals::string_view_literals;
        const std::string req = absl::StrCat("AT+REMOTESMS="sv, BinToHex(pdu.data), "\r"sv);

        absl::Status status = SendModemRequest(*socket, req);
        if (!status.ok()) {
            return status;
        }
    }

    return absl::OkStatus();
}

absl::StatusOr<SharedFD> SendCallRequest(const int serverPort,
                                         const ModemSimulatorClient::ModemCallState mcs,
                                         const std::string_view number) {
    absl::StatusOr<SharedFD> socket = ConnectToSimulator(serverPort);
    if (!socket.ok()) {
        return socket;
    }

    using namespace std::literals::string_view_literals;
    const std::string req = absl::StrCat("AT+REMOTECALL="sv, static_cast<unsigned>(mcs),
                                         ",0,0,\""sv, number, "\",129\r"sv);

    absl::Status status = SendModemRequest(*socket, req);
    if (!status.ok()) {
        return status;
    }

    return socket;
}

}  // namespace

ModemSimulatorClient::ModemCall::ModemCall(SharedFD modemConn) {
    SharedFD cancelListener;
    if (!SharedFD::Pipe(&cancelListener, &cancelator_)) {
        LOG(FATAL) << "Could not create a pipe";
    }

    socketThread_ = std::thread(
            [this, modemConn = std::move(modemConn), cancelListener = std::move(cancelListener)]() {
                std::string requestBuf;

                while (true) {
                    cuttlefish::SharedFDSet readSet;
                    readSet.Set(modemConn);
                    readSet.Set(cancelListener);

                    const int nfds = cuttlefish::Select(&readSet, nullptr, nullptr, nullptr);
                    if (nfds < 0) {
                        break;
                    } else if (nfds == 0) {
                        continue;
                    }

                    if (readSet.IsSet(modemConn)) {
                        if (!ProcessCallData(modemConn, requestBuf)) {
                            break;
                        }
                    } else if (readSet.IsSet(cancelListener)) {
                        break;
                    }
                }

                const absl::MutexLock lock(mtx_);
                state_ = ModemCallState::HANGUP;
            });
}

ModemSimulatorClient::ModemCall::~ModemCall() {
    cancelator_->Write("q", 1);
    socketThread_.join();
}

bool ModemSimulatorClient::ModemCall::ProcessCallData(const cuttlefish::SharedFD& socket,
                                                      std::string& requestBuf) {
    char buf[16];
    const ssize_t nBytes = socket->Read(buf, sizeof(buf));
    if (nBytes < 0) {
        return false;
    }

    for (unsigned i = 0; i < unsigned(nBytes); ++i) {
        const char c = buf[i];

        if (c == '\r') {
            return ProcessRequest(std::move(requestBuf));
        } else if (requestBuf.size() > kMaxRequestSize) {
            return false;
        } else {
            requestBuf.push_back(c);
        }
    }

    return true;
}

bool ModemSimulatorClient::ModemCall::ProcessRequest(std::string request) {
    using namespace std::literals::string_view_literals;
    if ((request == "OK"sv) || (request == "KO"sv)) {
        return false;
    } else {
        return true;
    }
}

void ModemSimulatorClient::ModemCall::UpdateState(const ModemCallState mcs) {
    const absl::MutexLock lock(mtx_);
    if (state_ != ModemCallState::HANGUP) {
        state_ = mcs;
        if (mcs == ModemCallState::HANGUP) {
            cancelator_->Write("q", 1);
        }
    }
}

ModemSimulatorClient::ModemCallState ModemSimulatorClient::ModemCall::GetState() const {
    const absl::MutexLock lock(mtx_);
    return state_;
}

ModemSimulatorClient::ModemSimulatorClient(int serverPort) : serverPort_(serverPort) {}

absl::StatusOr<CellInfo> ModemSimulatorClient::SetCellInfo(const CellInfo& ci) {
    absl::StatusOr<SharedFD> socket = ConnectToSimulator(serverPort_);
    if (!socket.ok()) {
        return socket.status();
    }

    if (ci.standard != CellStandard::UNKNOWN) {
        if (const auto status = SetCellStandard(*socket, ci.standard); !status.ok()) {
            return status;
        }
    }

    if (ci.voiceStatus != CellStatus::UNKNOWN) {
        if (const auto status = SetVoiceStatus(*socket, ci.voiceStatus); !status.ok()) {
            return status;
        }
    }

    if (const auto status = SetSignalStrength(*socket, ci.signalStrength); !status.ok()) {
        return status;
    }

    return ci;
}

absl::StatusOr<CellInfo> ModemSimulatorClient::GetCellInfo() {
    return absl::UnimplementedError("`GetCellInfo` is not yet implemented.");
}

absl::StatusOr<Call> ModemSimulatorClient::CreateCall(const Call& call) {
    auto activeCall = SendCallRequest(serverPort_, ModemCallState::INCOMING, call.number);
    if (!activeCall.ok()) {
        return activeCall.status();
    }

    const absl::MutexLock lock(mtx_);
    const auto [where, inserted] = calls_.insert({call.number, {}});
    if (!inserted) {
        return absl::InternalError(
                absl::StrCat("A call with '", call.number, "' is already in progress."));
    }

    where->second = std::make_unique<ModemCall>(*std::move(activeCall));

    Call result = call;
    result.state = IModemSimulatorClient::CallState::ACTIVE;
    return result;
}

absl::StatusOr<Call> ModemSimulatorClient::UpdateCall(const Call& call) {
    if (call.state == CallState::UNSPECIFIED) {
        return absl::InvalidArgumentError("CallState::UNSPECIFIED");
    }

    const ModemCallState mcs = ToModemCallState(call.state);
    auto activeCall = SendCallRequest(serverPort_, mcs, call.number);
    if (!activeCall.ok()) {
        return activeCall.status();
    }

    const absl::MutexLock lock(mtx_);
    const auto i = calls_.find(call.number);
    if (i == calls_.end()) {
        return absl::InternalError(absl::StrCat("The '", call.number, "' is not found."));
    }

    i->second->UpdateState(mcs);
    return call;
}

absl::Status ModemSimulatorClient::DeleteCall(const Call& call) {
    SendCallRequest(serverPort_, ModemCallState::HANGUP, call.number).IgnoreError();

    const absl::MutexLock lock(mtx_);
    if (calls_.erase(call.number) > 0) {
        return absl::OkStatus();
    } else {
        return absl::InvalidArgumentError(absl::StrCat("The '", call.number, "' is not found."));
    }
}

absl::StatusOr<std::vector<Call>> ModemSimulatorClient::ListCalls() {
    std::vector<Call> result;

    absl::MutexLock lock(mtx_);
    result.reserve(calls_.size());

    for (const auto& kv : calls_) {
        Call call = {
            .number = kv.first,
            .direction = CallDirection::INBOUND,
            .state = ToCallState(kv.second->GetState()),
        };

        result.push_back(std::move(call));
    }

    return result;
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
    absl::StatusOr<SharedFD> socket = ConnectToSimulator(serverPort_);
    if (!socket.ok()) {
        return socket.status();
    }

    using namespace std::literals::string_view_literals;
    return SendModemRequest(*socket, "AT+REMOTETIMEUPDATE: \r"sv);
}

}  // namespace goldfish::modem_simulator
