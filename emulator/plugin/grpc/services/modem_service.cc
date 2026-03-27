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

#include "android/emulation/control/absl_status_translate.h"
#include "goldfish/parsing/hexbin.h"

namespace android {
namespace emulation {
namespace control {
namespace incubating {

using goldfish::parsing::HexToBin;
using client_type = goldfish::modem_simulator::IModemSimulatorClient;

namespace {

client_type::CellStandard ToClient(const CellInfo::CellStandard cellStandard) {
    switch (cellStandard) {
    case CellInfo::CELL_STANDARD_UNKNOWN:
        return client_type::CellStandard::UNKNOWN;
    case CellInfo::CELL_STANDARD_GSM:
        return client_type::CellStandard::GSM;
    case CellInfo::CELL_STANDARD_HSCSD:
        return client_type::CellStandard::HSCSD;
    case CellInfo::CELL_STANDARD_GPRS:
        return client_type::CellStandard::GPRS;
    case CellInfo::CELL_STANDARD_EDGE:
        return client_type::CellStandard::EDGE;
    case CellInfo::CELL_STANDARD_UMTS:
        return client_type::CellStandard::UMTS;
    case CellInfo::CELL_STANDARD_HSDPA:
        return client_type::CellStandard::HSDPA;
    case CellInfo::CELL_STANDARD_LTE:
        return client_type::CellStandard::LTE;
    case CellInfo::CELL_STANDARD_FULL:
        return client_type::CellStandard::FULL;
    case CellInfo::CELL_STANDARD_5G:
        return client_type::CellStandard::NR_5G;
    default:
        return client_type::CellStandard::UNKNOWN;
    }
}

CellInfo::CellStandard ToProto(const client_type::CellStandard cellStandard) {
    switch (cellStandard) {
    case client_type::CellStandard::UNKNOWN:
        return CellInfo::CELL_STANDARD_UNKNOWN;
    case client_type::CellStandard::GSM:
        return CellInfo::CELL_STANDARD_GSM;
    case client_type::CellStandard::HSCSD:
        return CellInfo::CELL_STANDARD_HSCSD;
    case client_type::CellStandard::GPRS:
        return CellInfo::CELL_STANDARD_GPRS;
    case client_type::CellStandard::EDGE:
        return CellInfo::CELL_STANDARD_EDGE;
    case client_type::CellStandard::UMTS:
        return CellInfo::CELL_STANDARD_UMTS;
    case client_type::CellStandard::HSDPA:
        return CellInfo::CELL_STANDARD_HSDPA;
    case client_type::CellStandard::LTE:
        return CellInfo::CELL_STANDARD_LTE;
    case client_type::CellStandard::FULL:
        return CellInfo::CELL_STANDARD_FULL;
    case client_type::CellStandard::NR_5G:
        return CellInfo::CELL_STANDARD_5G;
    }

    return CellInfo::CELL_STANDARD_UNKNOWN;
}

client_type::CellStatus ToClient(const CellInfo::CellStatus status) {
    switch (status) {
    case CellInfo::CELL_STATUS_UNKNOWN:
        return client_type::CellStatus::UNKNOWN;
    case CellInfo::CELL_STATUS_HOME:
        return client_type::CellStatus::HOME;
    case CellInfo::CELL_STATUS_ROAMING:
        return client_type::CellStatus::ROAMING;
    case CellInfo::CELL_STATUS_SEARCHING:
        return client_type::CellStatus::SEARCHING;
    case CellInfo::CELL_STATUS_DENIED:
        return client_type::CellStatus::DENIED;
    case CellInfo::CELL_STATUS_UNREGISTERED:
        return client_type::CellStatus::UNREGISTERED;
    default:
        return client_type::CellStatus::UNKNOWN;
    }
}

CellInfo::CellStatus ToProto(const client_type::CellStatus status) {
    switch (status) {
    case client_type::CellStatus::UNKNOWN:
        return CellInfo::CELL_STATUS_UNKNOWN;
    case client_type::CellStatus::HOME:
        return CellInfo::CELL_STATUS_HOME;
    case client_type::CellStatus::ROAMING:
        return CellInfo::CELL_STATUS_ROAMING;
    case client_type::CellStatus::SEARCHING:
        return CellInfo::CELL_STATUS_SEARCHING;
    case client_type::CellStatus::DENIED:
        return CellInfo::CELL_STATUS_DENIED;
    case client_type::CellStatus::UNREGISTERED:
        return CellInfo::CELL_STATUS_UNREGISTERED;
    }
    return CellInfo::CELL_STATUS_UNKNOWN;
}

client_type::MeterStatus ToClient(const CellInfo::CellMeterStatus status) {
    switch (status) {
    case CellInfo::CELL_METER_STATUS_UNKNOWN:
        return client_type::MeterStatus::UNKNOWN;
    case CellInfo::CELL_METER_STATUS_METERED:
        return client_type::MeterStatus::METERED;
    case CellInfo::CELL_METER_STATUS_TEMPORARILY_NOT_METERED:
        return client_type::MeterStatus::TEMPORARILY_NOT_METERED;
    default:
        return client_type::MeterStatus::UNKNOWN;
    }
}

CellInfo::CellMeterStatus ToProto(const client_type::MeterStatus status) {
    switch (status) {
    case client_type::MeterStatus::UNKNOWN:
        return CellInfo::CELL_METER_STATUS_UNKNOWN;
    case client_type::MeterStatus::METERED:
        return CellInfo::CELL_METER_STATUS_METERED;
    case client_type::MeterStatus::TEMPORARILY_NOT_METERED:
        return CellInfo::CELL_METER_STATUS_TEMPORARILY_NOT_METERED;
    }
    return CellInfo::CELL_METER_STATUS_UNKNOWN;
}

client_type::SimStatus ToClient(const CellInfo::CellSimStatus status) {
    switch (status) {
    case CellInfo::CELL_SIM_STATUS_UNKNOWN:
        return client_type::SimStatus::UNKNOWN;
    case CellInfo::CELL_SIM_STATUS_NOT_PRESENT:
        return client_type::SimStatus::NOT_PRESENT;
    case CellInfo::CELL_SIM_STATUS_PRESENT:
        return client_type::SimStatus::PRESENT;
    default:
        return client_type::SimStatus::UNKNOWN;
    }
}

CellInfo::CellSimStatus ToProto(const client_type::SimStatus status) {
    switch (status) {
    case client_type::SimStatus::UNKNOWN:
        return CellInfo::CELL_SIM_STATUS_UNKNOWN;
    case client_type::SimStatus::NOT_PRESENT:
        return CellInfo::CELL_SIM_STATUS_NOT_PRESENT;
    case client_type::SimStatus::PRESENT:
        return CellInfo::CELL_SIM_STATUS_PRESENT;
    }
    return CellInfo::CELL_SIM_STATUS_UNKNOWN;
}

client_type::SignalStrength ToClient(const CellSignalStrength::CellSignalLevel level) {
    switch (level) {
    case CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN:
        return client_type::SignalStrength::NONE_OR_UNKNOWN;
    case CellSignalStrength::SIGNAL_STRENGTH_POOR:
        return client_type::SignalStrength::POOR;
    case CellSignalStrength::SIGNAL_STRENGTH_MODERATE:
        return client_type::SignalStrength::MODERATE;
    case CellSignalStrength::SIGNAL_STRENGTH_GOOD:
        return client_type::SignalStrength::GOOD;
    case CellSignalStrength::SIGNAL_STRENGTH_GREAT:
        return client_type::SignalStrength::GREAT;
    default:
        return client_type::SignalStrength::NONE_OR_UNKNOWN;
    }
}

CellSignalStrength::CellSignalLevel ToProto(const client_type::SignalStrength level) {
    switch (level) {
    case client_type::SignalStrength::NONE_OR_UNKNOWN:
        return CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN;
    case client_type::SignalStrength::POOR:
        return CellSignalStrength::SIGNAL_STRENGTH_POOR;
    case client_type::SignalStrength::MODERATE:
        return CellSignalStrength::SIGNAL_STRENGTH_MODERATE;
    case client_type::SignalStrength::GOOD:
        return CellSignalStrength::SIGNAL_STRENGTH_GOOD;
    case client_type::SignalStrength::GREAT:
        return CellSignalStrength::SIGNAL_STRENGTH_GREAT;
    }
    return CellSignalStrength::SIGNAL_STRENGTH_NONE_OR_UNKNOWN;
}

client_type::CallState ToClient(const Call::CallState state) {
    switch (state) {
    case Call::CALL_STATE_UNSPECIFIED:
        return client_type::CallState::UNSPECIFIED;
    case Call::CALL_STATE_ACTIVE:
        return client_type::CallState::ACTIVE;
    case Call::CALL_STATE_HELD:
        return client_type::CallState::HELD;
    case Call::CALL_STATE_DIALING:
        return client_type::CallState::DIALING;
    case Call::CALL_STATE_ALERTING:
        return client_type::CallState::ALERTING;
    case Call::CALL_STATE_INCOMING:
        return client_type::CallState::INCOMING;
    case Call::CALL_STATE_WAITING:
        return client_type::CallState::WAITING;
    default:
        return client_type::CallState::UNSPECIFIED;
    }
}

Call::CallState ToProto(const client_type::CallState state) {
    switch (state) {
    case client_type::CallState::UNSPECIFIED:
        return Call::CALL_STATE_UNSPECIFIED;
    case client_type::CallState::ACTIVE:
        return Call::CALL_STATE_ACTIVE;
    case client_type::CallState::HELD:
        return Call::CALL_STATE_HELD;
    case client_type::CallState::DIALING:
        return Call::CALL_STATE_DIALING;
    case client_type::CallState::ALERTING:
        return Call::CALL_STATE_ALERTING;
    case client_type::CallState::INCOMING:
        return Call::CALL_STATE_INCOMING;
    case client_type::CallState::WAITING:
        return Call::CALL_STATE_WAITING;
    }
    return Call::CALL_STATE_UNSPECIFIED;
}

client_type::CallDirection ToClient(const Call::CallDirection direction) {
    switch (direction) {
    case Call::CALL_DIRECTION_UNSPECIFIED:
        return client_type::CallDirection::UNSPECIFIED;
    case Call::CALL_DIRECTION_OUTBOUND:
        return client_type::CallDirection::OUTBOUND;
    case Call::CALL_DIRECTION_INBOUND:
        return client_type::CallDirection::INBOUND;
    default:
        return client_type::CallDirection::UNSPECIFIED;
    }
}

Call::CallDirection ToProto(const client_type::CallDirection direction) {
    switch (direction) {
    case client_type::CallDirection::UNSPECIFIED:
        return Call::CALL_DIRECTION_UNSPECIFIED;
    case client_type::CallDirection::OUTBOUND:
        return Call::CALL_DIRECTION_OUTBOUND;
    case client_type::CallDirection::INBOUND:
        return Call::CALL_DIRECTION_INBOUND;
    }
    return Call::CALL_DIRECTION_UNSPECIFIED;
}

client_type::CellInfo ToClient(const CellInfo& proto) {
    client_type::CellInfo client;
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

CellInfo ToProto(const client_type::CellInfo& client) {
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

client_type::Call ToClient(const Call& proto) {
    client_type::Call client;
    client.number = proto.number();
    client.direction = ToClient(proto.direction());
    client.state = ToClient(proto.state());
    return client;
}

Call ToProto(const client_type::Call& client) {
    Call proto;
    proto.set_number(client.number);
    proto.set_direction(ToProto(client.direction));
    proto.set_state(ToProto(client.state));
    return proto;
}

}  // namespace

ModemServiceImpl::ModemServiceImpl(
        std::unique_ptr<goldfish::modem_simulator::IModemSimulatorClient> client)
        : client_(std::move(client)) {}

::grpc::Status ModemServiceImpl::setCellInfo(::grpc::ServerContext* /*context*/,
                                             const CellInfo* request, CellInfo* response) {
    auto result = client_->SetCellInfo(ToClient(*request));
    if (result.ok()) {
        *response = ToProto(*result);
        return ::grpc::Status::OK;
    }
    return AbslStatusToGrpcStatus(result.status());
}

::grpc::Status ModemServiceImpl::getCellInfo(::grpc::ServerContext* /*context*/,
                                             const ::google::protobuf::Empty* /*request*/,
                                             CellInfo* response) {
    auto result = client_->GetCellInfo();
    if (result.ok()) {
        *response = ToProto(*result);
        return ::grpc::Status::OK;
    }
    return AbslStatusToGrpcStatus(result.status());
}

::grpc::Status ModemServiceImpl::createCall(::grpc::ServerContext* /*context*/, const Call* request,
                                            Call* response) {
    auto result = client_->CreateCall(ToClient(*request));
    if (result.ok()) {
        *response = ToProto(*result);
        return ::grpc::Status::OK;
    }
    return AbslStatusToGrpcStatus(result.status());
}

::grpc::Status ModemServiceImpl::updateCall(::grpc::ServerContext* /*context*/, const Call* request,
                                            Call* response) {
    auto result = client_->UpdateCall(ToClient(*request));
    if (result.ok()) {
        *response = ToProto(*result);
        return ::grpc::Status::OK;
    }
    return AbslStatusToGrpcStatus(result.status());
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
            return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                                  "Can't parse `encodedmessage`");
        }

        result = client_->ReceiveSmsEncoded(*std::move(binary));
    } else {
        return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, "No message to deliver");
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
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android