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
#include "android/emulation/control/incubating/modem_service.h"

#include "absl/strings/str_cat.h"

#include "android/emulation/control/absl_status_translate.h"
#include "goldfish/parsing/hexbin.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

using ::goldfish::parsing::HexToBin;
using ClientType = ::goldfish::modem_simulator::IModemSimulatorClient;

namespace {

ClientType::CellStandard ToClient(const CellInfo::CellStandard cell_standard) {
    switch (cell_standard) {
    case CellInfo::CELL_STANDARD_UNKNOWN:
        return ClientType::CellStandard::UNKNOWN;
    case CellInfo::CELL_STANDARD_GSM:
        return ClientType::CellStandard::GSM;
    case CellInfo::CELL_STANDARD_HSCSD:
        return ClientType::CellStandard::HSCSD;
    case CellInfo::CELL_STANDARD_GPRS:
        return ClientType::CellStandard::GPRS;
    case CellInfo::CELL_STANDARD_EDGE:
        return ClientType::CellStandard::EDGE;
    case CellInfo::CELL_STANDARD_UMTS:
        return ClientType::CellStandard::UMTS;
    case CellInfo::CELL_STANDARD_HSDPA:
        return ClientType::CellStandard::HSDPA;
    case CellInfo::CELL_STANDARD_LTE:
        return ClientType::CellStandard::LTE;
    case CellInfo::CELL_STANDARD_FULL:
        return ClientType::CellStandard::FULL;
    case CellInfo::CELL_STANDARD_5G:
        return ClientType::CellStandard::NR_5G;
    default:
        return ClientType::CellStandard::UNKNOWN;
    }
}

CellInfo::CellStandard ToProto(const ClientType::CellStandard cell_standard) {
    switch (cell_standard) {
    case ClientType::CellStandard::UNKNOWN:
        return CellInfo::CELL_STANDARD_UNKNOWN;
    case ClientType::CellStandard::GSM:
        return CellInfo::CELL_STANDARD_GSM;
    case ClientType::CellStandard::HSCSD:
        return CellInfo::CELL_STANDARD_HSCSD;
    case ClientType::CellStandard::GPRS:
        return CellInfo::CELL_STANDARD_GPRS;
    case ClientType::CellStandard::EDGE:
        return CellInfo::CELL_STANDARD_EDGE;
    case ClientType::CellStandard::UMTS:
        return CellInfo::CELL_STANDARD_UMTS;
    case ClientType::CellStandard::HSDPA:
        return CellInfo::CELL_STANDARD_HSDPA;
    case ClientType::CellStandard::LTE:
        return CellInfo::CELL_STANDARD_LTE;
    case ClientType::CellStandard::FULL:
        return CellInfo::CELL_STANDARD_FULL;
    case ClientType::CellStandard::NR_5G:
        return CellInfo::CELL_STANDARD_5G;
    default:
        return CellInfo::CELL_STANDARD_UNKNOWN;
    }
}

ClientType::CellStatus ToClient(const CellInfo::CellStatus status) {
    switch (status) {
    case CellInfo::CELL_STATUS_UNKNOWN:
        return ClientType::CellStatus::UNKNOWN;
    case CellInfo::CELL_STATUS_HOME:
        return ClientType::CellStatus::HOME;
    case CellInfo::CELL_STATUS_ROAMING:
        return ClientType::CellStatus::ROAMING;
    case CellInfo::CELL_STATUS_SEARCHING:
        return ClientType::CellStatus::SEARCHING;
    case CellInfo::CELL_STATUS_DENIED:
        return ClientType::CellStatus::DENIED;
    case CellInfo::CELL_STATUS_UNREGISTERED:
        return ClientType::CellStatus::UNREGISTERED;
    default:
        return ClientType::CellStatus::UNKNOWN;
    }
}

CellInfo::CellStatus ToProto(const ClientType::CellStatus status) {
    switch (status) {
    case ClientType::CellStatus::UNKNOWN:
        return CellInfo::CELL_STATUS_UNKNOWN;
    case ClientType::CellStatus::HOME:
        return CellInfo::CELL_STATUS_HOME;
    case ClientType::CellStatus::ROAMING:
        return CellInfo::CELL_STATUS_ROAMING;
    case ClientType::CellStatus::SEARCHING:
        return CellInfo::CELL_STATUS_SEARCHING;
    case ClientType::CellStatus::DENIED:
        return CellInfo::CELL_STATUS_DENIED;
    case ClientType::CellStatus::UNREGISTERED:
        return CellInfo::CELL_STATUS_UNREGISTERED;
    default:
        return CellInfo::CELL_STATUS_UNKNOWN;
    }
}

ClientType::MeterStatus ToClient(const CellInfo::CellMeterStatus status) {
    switch (status) {
    case CellInfo::CELL_METER_STATUS_UNKNOWN:
        return ClientType::MeterStatus::UNKNOWN;
    case CellInfo::CELL_METER_STATUS_METERED:
        return ClientType::MeterStatus::METERED;
    case CellInfo::CELL_METER_STATUS_TEMPORARILY_NOT_METERED:
        return ClientType::MeterStatus::TEMPORARILY_NOT_METERED;
    default:
        return ClientType::MeterStatus::UNKNOWN;
    }
}

CellInfo::CellMeterStatus ToProto(const ClientType::MeterStatus status) {
    switch (status) {
    case ClientType::MeterStatus::UNKNOWN:
        return CellInfo::CELL_METER_STATUS_UNKNOWN;
    case ClientType::MeterStatus::METERED:
        return CellInfo::CELL_METER_STATUS_METERED;
    case ClientType::MeterStatus::TEMPORARILY_NOT_METERED:
        return CellInfo::CELL_METER_STATUS_TEMPORARILY_NOT_METERED;
    default:
        return CellInfo::CELL_METER_STATUS_UNKNOWN;
    }
}

ClientType::SimStatus ToClient(const CellInfo::CellSimStatus status) {
    switch (status) {
    case CellInfo::CELL_SIM_STATUS_UNKNOWN:
        return ClientType::SimStatus::UNKNOWN;
    case CellInfo::CELL_SIM_STATUS_NOT_PRESENT:
        return ClientType::SimStatus::NOT_PRESENT;
    case CellInfo::CELL_SIM_STATUS_PRESENT:
        return ClientType::SimStatus::PRESENT;
    default:
        return ClientType::SimStatus::UNKNOWN;
    }
}

CellInfo::CellSimStatus ToProto(const ClientType::SimStatus status) {
    switch (status) {
    case ClientType::SimStatus::UNKNOWN:
        return CellInfo::CELL_SIM_STATUS_UNKNOWN;
    case ClientType::SimStatus::NOT_PRESENT:
        return CellInfo::CELL_SIM_STATUS_NOT_PRESENT;
    case ClientType::SimStatus::PRESENT:
        return CellInfo::CELL_SIM_STATUS_PRESENT;
    default:
        return CellInfo::CELL_SIM_STATUS_UNKNOWN;
    }
}

ClientType::SignalStrength ToClient(const CellSignalStrength::CellSignalLevel level) {
    switch (level) {
    case CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN:
        return ClientType::SignalStrength::NONE_OR_UNKNOWN;
    case CellSignalStrength::SIGNAL_STRENGTH_POOR:
        return ClientType::SignalStrength::POOR;
    case CellSignalStrength::SIGNAL_STRENGTH_MODERATE:
        return ClientType::SignalStrength::MODERATE;
    case CellSignalStrength::SIGNAL_STRENGTH_GOOD:
        return ClientType::SignalStrength::GOOD;
    case CellSignalStrength::SIGNAL_STRENGTH_GREAT:
        return ClientType::SignalStrength::GREAT;
    default:
        return ClientType::SignalStrength::NONE_OR_UNKNOWN;
    }
}

CellSignalStrength::CellSignalLevel ToProto(const ClientType::SignalStrength level) {
    switch (level) {
    case ClientType::SignalStrength::NONE_OR_UNKNOWN:
        return CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN;
    case ClientType::SignalStrength::POOR:
        return CellSignalStrength::SIGNAL_STRENGTH_POOR;
    case ClientType::SignalStrength::MODERATE:
        return CellSignalStrength::SIGNAL_STRENGTH_MODERATE;
    case ClientType::SignalStrength::GOOD:
        return CellSignalStrength::SIGNAL_STRENGTH_GOOD;
    case ClientType::SignalStrength::GREAT:
        return CellSignalStrength::SIGNAL_STRENGTH_GREAT;
    default:
        return CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN;
    }
}

ClientType::CallState ToClient(const Call::CallState state) {
    switch (state) {
    case Call::CALL_STATE_UNSPECIFIED:
        return ClientType::CallState::UNSPECIFIED;
    case Call::CALL_STATE_ACTIVE:
        return ClientType::CallState::ACTIVE;
    case Call::CALL_STATE_HELD:
        return ClientType::CallState::HELD;
    case Call::CALL_STATE_DIALING:
        return ClientType::CallState::DIALING;
    case Call::CALL_STATE_ALERTING:
        return ClientType::CallState::ALERTING;
    case Call::CALL_STATE_INCOMING:
        return ClientType::CallState::INCOMING;
    case Call::CALL_STATE_WAITING:
        return ClientType::CallState::WAITING;
    default:
        return ClientType::CallState::UNSPECIFIED;
    }
}

Call::CallState ToProto(const ClientType::CallState state) {
    switch (state) {
    case ClientType::CallState::UNSPECIFIED:
        return Call::CALL_STATE_UNSPECIFIED;
    case ClientType::CallState::ACTIVE:
        return Call::CALL_STATE_ACTIVE;
    case ClientType::CallState::HELD:
        return Call::CALL_STATE_HELD;
    case ClientType::CallState::DIALING:
        return Call::CALL_STATE_DIALING;
    case ClientType::CallState::ALERTING:
        return Call::CALL_STATE_ALERTING;
    case ClientType::CallState::INCOMING:
        return Call::CALL_STATE_INCOMING;
    case ClientType::CallState::WAITING:
        return Call::CALL_STATE_WAITING;
    default:
        return Call::CALL_STATE_UNSPECIFIED;
    }
}

ClientType::CallDirection ToClient(const Call::CallDirection direction) {
    switch (direction) {
    case Call::CALL_DIRECTION_UNSPECIFIED:
        return ClientType::CallDirection::UNSPECIFIED;
    case Call::CALL_DIRECTION_OUTBOUND:
        return ClientType::CallDirection::OUTBOUND;
    case Call::CALL_DIRECTION_INBOUND:
        return ClientType::CallDirection::INBOUND;
    default:
        return ClientType::CallDirection::UNSPECIFIED;
    }
}

Call::CallDirection ToProto(const ClientType::CallDirection direction) {
    switch (direction) {
    case ClientType::CallDirection::UNSPECIFIED:
        return Call::CALL_DIRECTION_UNSPECIFIED;
    case ClientType::CallDirection::OUTBOUND:
        return Call::CALL_DIRECTION_OUTBOUND;
    case ClientType::CallDirection::INBOUND:
        return Call::CALL_DIRECTION_INBOUND;
    default:
        return Call::CALL_DIRECTION_UNSPECIFIED;
    }
}

ClientType::CellInfo ToClient(const CellInfo& proto) {
    ClientType::CellInfo client;
    if (proto.has_cell_identity()) {
        if (proto.cell_identity().has_operatoralphalong()) {
            client.identity.operatorLong = proto.cell_identity().operatoralphalong().value();
        }
        if (proto.cell_identity().has_operatoralphashort()) {
            client.identity.operatorShort = proto.cell_identity().operatoralphashort().value();
        }
    }
    client.standard = ToClient(proto.cell_standard());
    client.voiceStatus = ToClient(proto.cell_status_voice());
    client.dataStatus = ToClient(proto.cell_status_data());

    if (proto.has_cell_signal_strength()) {
        if (proto.cell_signal_strength().has_level()) {
            client.signalStrength = ToClient(proto.cell_signal_strength().level());
        }
    }
    client.meterStatus = ToClient(proto.cell_meter_status());
    client.simStatus = ToClient(proto.sim_status());
    return client;
}

CellInfo ToProto(const ClientType::CellInfo& client) {
    CellInfo proto;
    if (!client.identity.operatorLong.empty()) {
        proto.mutable_cell_identity()->mutable_operatoralphalong()->set_value(
                client.identity.operatorLong);
    }
    if (!client.identity.operatorShort.empty()) {
        proto.mutable_cell_identity()->mutable_operatoralphashort()->set_value(
                client.identity.operatorShort);
    }
    proto.set_cell_standard(ToProto(client.standard));
    proto.set_cell_status_voice(ToProto(client.voiceStatus));
    proto.set_cell_status_data(ToProto(client.dataStatus));
    proto.mutable_cell_signal_strength()->set_level(ToProto(client.signalStrength));
    proto.set_cell_meter_status(ToProto(client.meterStatus));
    proto.set_sim_status(ToProto(client.simStatus));
    return proto;
}

ClientType::Call ToClient(const Call& proto) {
    ClientType::Call client;
    client.number = proto.number();
    client.direction = ToClient(proto.direction());
    client.state = ToClient(proto.state());
    return client;
}

Call ToProto(const ClientType::Call& client) {
    Call proto;
    proto.set_number(client.number);
    proto.set_direction(ToProto(client.direction));
    proto.set_state(ToProto(client.state));
    return proto;
}

template <typename ClientT, typename ProtoT>
::grpc::Status ToGrpcResponse(const absl::StatusOr<ClientT>& result, ProtoT* response) {
    if (!result.ok()) {
        return AbslStatusToGrpcStatus(result.status());
    }
    *response = ToProto(*result);
    return ::grpc::Status::OK;
}

}  // namespace

ModemServiceImpl::ModemServiceImpl(
        std::unique_ptr<goldfish::modem_simulator::IModemSimulatorClient> client)
        : client_(std::move(client)) {}

::grpc::Status ModemServiceImpl::setCellInfo(::grpc::ServerContext* /*context*/,
                                             const CellInfo* request, CellInfo* response) {
    return ToGrpcResponse(client_->SetCellInfo(ToClient(*request)), response);
}

::grpc::Status ModemServiceImpl::getCellInfo(::grpc::ServerContext* /*context*/,
                                             const ::google::protobuf::Empty* /*request*/,
                                             CellInfo* response) {
    return ToGrpcResponse(client_->GetCellInfo(), response);
}

::grpc::Status ModemServiceImpl::createCall(::grpc::ServerContext* /*context*/, const Call* request,
                                            Call* response) {
    return ToGrpcResponse(client_->CreateCall(ToClient(*request)), response);
}

::grpc::Status ModemServiceImpl::updateCall(::grpc::ServerContext* /*context*/, const Call* request,
                                            Call* response) {
    return ToGrpcResponse(client_->UpdateCall(ToClient(*request)), response);
}

::grpc::Status ModemServiceImpl::deleteCall(::grpc::ServerContext* /*context*/, const Call* request,
                                            ::google::protobuf::Empty* /*response*/) {
    return AbslStatusToGrpcStatus(client_->DeleteCall(ToClient(*request)));
}

::grpc::Status ModemServiceImpl::listCalls(::grpc::ServerContext* /*context*/,
                                           const ::google::protobuf::Empty* /*request*/,
                                           ActiveCalls* response) {
    auto result = client_->ListCalls();
    if (result.ok()) {
        for (const auto& call : *result) {
            *response->add_calls() = ToProto(call);
        }
        return ::grpc::Status::OK;
    }
    return AbslStatusToGrpcStatus(result.status());
}

::grpc::Status ModemServiceImpl::receiveSms(::grpc::ServerContext* /*context*/,
                                            const SmsMessage* request,
                                            ::google::protobuf::Empty* /*response*/) {
    absl::Status result;
    if (request->has_text()) {
        result = client_->ReceiveSmsUtf8(request->number(), request->text());
    } else if (request->has_encodedmessage()) {
        auto binary = HexToBin(request->encodedmessage());
        if (!binary) {
            const size_t encoded_len = request->encodedmessage().size();
            if (encoded_len % 2U != 0U) {
                return ::grpc::Status(
                        ::grpc::StatusCode::INVALID_ARGUMENT,
                        absl::StrCat("Failed to parse 'encodedMessage' in receiveSms: length ",
                                     encoded_len,
                                     " is odd, but hex-encoded SMS PDUs require an even number of "
                                     "hexadecimal digits (2 per byte). Provide a valid hex-encoded "
                                     "SMS PDU in 'encodedMessage', or set 'text' instead."));
            }
            return ::grpc::Status(
                    ::grpc::StatusCode::INVALID_ARGUMENT,
                    absl::StrCat("Failed to parse 'encodedMessage' in receiveSms: value of length ",
                                 encoded_len,
                                 " contains non-hexadecimal characters (expected only 0-9, a-f, or "
                                 "A-F). Provide a valid hex-encoded SMS PDU in 'encodedMessage', "
                                 "or set 'text' instead."));
        }

        result = client_->ReceiveSmsEncoded(*std::move(binary));
    } else {
        return ::grpc::Status(
                ::grpc::StatusCode::INVALID_ARGUMENT,
                "Failed to deliver SMS in receiveSms: neither 'text' nor 'encodedMessage' is set "
                "in SmsMessage. Set 'text' (UTF-8 message body) or 'encodedMessage' (hex-encoded "
                "SMS PDU).");
    }

    return AbslStatusToGrpcStatus(result);
}

::grpc::Status ModemServiceImpl::updateClock(::grpc::ServerContext* /*context*/,
                                             const ::google::protobuf::Empty* /*request*/,
                                             ::google::protobuf::Empty* /*response*/) {
    return AbslStatusToGrpcStatus(client_->UpdateClock());
}

::grpc::Status ModemServiceImpl::receivePhoneEvents(::grpc::ServerContext* /*context*/,
                                                    const ::google::protobuf::Empty* /*request*/,
                                                    ::grpc::ServerWriter<PhoneEvent>* /*writer*/) {
    return ::grpc::Status(
            ::grpc::StatusCode::UNIMPLEMENTED,
            "receivePhoneEvents is not implemented yet. Poll active calls with listCalls instead.");
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android