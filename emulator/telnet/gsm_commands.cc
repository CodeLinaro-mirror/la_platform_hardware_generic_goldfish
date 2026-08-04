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
#include "gsm_commands.h"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/emulation/control/absl_status_translate.h"
#include "console_context.h"
#include "modem_service.grpc.pb.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;

using android::emulation::control::incubating::ActiveCalls;
using android::emulation::control::incubating::Call;
using android::emulation::control::incubating::CellInfo;
using android::emulation::control::incubating::CellSignalStrength;

namespace {

bool CheckPhoneNumber(std::string_view number) {
    if (number.empty()) return false;
    for (char c : number) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '+' && c != '#') {
            return false;
        }
    }
    return true;
}

const char* CellStatusToString(CellInfo::CellStatus status) {
    switch (status) {
    case CellInfo::CELL_STATUS_UNREGISTERED:
        return "unregistered";
    case CellInfo::CELL_STATUS_HOME:
        return "home";
    case CellInfo::CELL_STATUS_ROAMING:
        return "roaming";
    case CellInfo::CELL_STATUS_SEARCHING:
        return "searching";
    case CellInfo::CELL_STATUS_DENIED:
        return "denied";
    default:
        return "<unknown>";
    }
}

absl::StatusOr<CellInfo::CellStatus> ParseCellStatus(std::string_view state) {
    if (state == "unregistered" || state == "off") return CellInfo::CELL_STATUS_UNREGISTERED;
    if (state == "home" || state == "on") return CellInfo::CELL_STATUS_HOME;
    if (state == "roaming") return CellInfo::CELL_STATUS_ROAMING;
    if (state == "searching") return CellInfo::CELL_STATUS_SEARCHING;
    if (state == "denied") return CellInfo::CELL_STATUS_DENIED;
    return absl::InvalidArgumentError(
            "bad GSM data state name, try 'help gsm data' for list of valid values");
}

const char* CallStateToString(Call::CallState state) {
    switch (state) {
    case Call::CALL_STATE_ACTIVE:
        return "active";
    case Call::CALL_STATE_HELD:
        return "held";
    case Call::CALL_STATE_ALERTING:
        return "ringing";
    case Call::CALL_STATE_WAITING:
        return "waiting";
    case Call::CALL_STATE_INCOMING:
        return "incoming";
    default:
        return "unknown";
    }
}

}  // namespace

void RegisterGsmCommands(CommandRegistryBuilder::NodeBuilder& gsm) {
    auto update_call_state = [](ConsoleContext& ctx, const std::string& remote_number,
                                Call::CallState state, const char* error_msg) {
        ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
        ASSIGN_OR_RETURN(auto context_list, ctx.NewContext());
        google::protobuf::Empty list_req;
        ActiveCalls active_calls;
        RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                stub->listCalls(context_list.get(), list_req, &active_calls)));

        bool found = false;
        for (const auto& call : active_calls.calls()) {
            if (call.number() == remote_number) {
                found = true;
                break;
            }
        }
        if (!found) {
            return absl::FailedPreconditionError(
                    absl::StrFormat("no current call to/from number '%s'", remote_number));
        }

        ASSIGN_OR_RETURN(auto context_upd, ctx.NewContext());
        Call request;
        request.set_number(remote_number);
        request.set_state(state);
        Call response;
        auto status = stub->updateCall(context_upd.get(), request, &response);
        if (!status.ok()) {
            return absl::FailedPreconditionError(error_msg);
        }
        return absl::OkStatus();
    };

    gsm.On("list" /* do_gsm_list */, "list current phone calls",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               google::protobuf::Empty request;
               ActiveCalls response;
               RETURN_IF_ERROR(
                       GrpcStatusToAbslStatus(stub->listCalls(context.get(), request, &response)));
               std::string result;
               for (const auto& call : response.calls()) {
                   auto dir = call.direction() == Call::CALL_DIRECTION_OUTBOUND ? "outbound to "
                                                                                : "inbound from";
                   if (!result.empty()) {
                       result += "\r\n";
                   }
                   absl::StrAppend(&result, absl::StrFormat("%s %-10s : %s", dir, call.number(),
                                                            CallStateToString(call.state())));
               }
               return result;
           });

    gsm.On("call" /* do_gsm_call */, "create inbound phone call",
           [](ConsoleContext& ctx, const std::string& phonenumber) {
               if (!CheckPhoneNumber(phonenumber)) {
                   return absl::InvalidArgumentError(
                           "bad phone number format, use digits, # and + only");
               }
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               Call request;
               request.set_number(phonenumber);
               request.set_direction(Call::CALL_DIRECTION_INBOUND);
               request.set_state(Call::CALL_STATE_INCOMING);
               Call response;
               return GrpcStatusToAbslStatus(stub->createCall(context.get(), request, &response));
           });

    gsm.On("busy" /* do_gsm_busy */, "close waiting outbound call as busy",
           [](ConsoleContext& ctx, const std::string& remote_number) {
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context_list, ctx.NewContext());
               google::protobuf::Empty list_req;
               ActiveCalls active_calls;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       stub->listCalls(context_list.get(), list_req, &active_calls)));

               bool found_outbound = false;
               for (const auto& call : active_calls.calls()) {
                   if (call.number() == remote_number &&
                       call.direction() == Call::CALL_DIRECTION_OUTBOUND) {
                       found_outbound = true;
                       break;
                   }
               }
               if (!found_outbound) {
                   return absl::FailedPreconditionError(absl::StrFormat(
                           "no current outbound call to number '%s' (call 0x0)", remote_number));
               }

               ASSIGN_OR_RETURN(auto context_del, ctx.NewContext());
               Call request;
               request.set_number(remote_number);
               google::protobuf::Empty unused;
               auto status = stub->deleteCall(context_del.get(), request, &unused);
               if (!status.ok()) {
                   return absl::FailedPreconditionError("could not cancel this number");
               }
               return absl::OkStatus();
           });

    gsm.On("hold" /* do_gsm_hold */, "change the state of an outbound call to 'held'",
           [update_call_state](ConsoleContext& ctx, const std::string& remote_number) {
               return update_call_state(ctx, remote_number, Call::CALL_STATE_HELD,
                                        "could put this call on hold");
           });

    gsm.On("accept" /* do_gsm_accept */, "change the state of an outbound call to 'active'",
           [update_call_state](ConsoleContext& ctx, const std::string& remote_number) {
               return update_call_state(ctx, remote_number, Call::CALL_STATE_ACTIVE,
                                        "could not activate this call");
           });

    gsm.On("cancel" /* do_gsm_cancel */, "disconnect an inbound or outbound phone call",
           [](ConsoleContext& ctx, const std::string& remote_number) {
               if (!CheckPhoneNumber(remote_number)) {
                   return absl::InvalidArgumentError(
                           "bad phone number format, use digits, # and + only");
               }
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               Call request;
               request.set_number(remote_number);
               google::protobuf::Empty unused;
               auto status = stub->deleteCall(context.get(), request, &unused);
               if (!status.ok()) {
                   return absl::FailedPreconditionError("could not cancel this number");
               }
               return absl::OkStatus();
           });

    gsm.On("data" /* do_gsm_data */, "modify data connection state",
           "the 'gsm data <state>' allows you to change the state of your GPRS connection\r\n"
           "valid values for <state> are the following:\r\n\r\n"
           "  unregistered    no network available\r\n"
           "  home            on local network, non-roaming\r\n"
           "  roaming         on roaming network\r\n"
           "  searching       searching networks\r\n"
           "  denied          emergency calls only\r\n"
           "  off             same as 'unregistered'\r\n"
           "  on              same as 'home'\r\n",
           [](ConsoleContext& ctx, const std::string& state) {
               ASSIGN_OR_RETURN(auto cell_status, ParseCellStatus(state));
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               CellInfo request;
               request.set_cell_status_data(cell_status);
               CellInfo response;
               return GrpcStatusToAbslStatus(stub->setCellInfo(context.get(), request, &response));
           });

    gsm.On("meter" /* do_gsm_meter */, "modify mobile data meterness",
           "the 'gsm meter <on|off>' allows you to change the meterness of your mobile data "
           "plan\r\n",
           [](ConsoleContext& ctx, bool metered) {
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               CellInfo request;
               request.set_cell_meter_status(
                       metered ? CellInfo::CELL_METER_STATUS_METERED
                               : CellInfo::CELL_METER_STATUS_TEMPORARILY_NOT_METERED);
               CellInfo response;
               return GrpcStatusToAbslStatus(stub->setCellInfo(context.get(), request, &response));
           });

    gsm.On("voice" /* do_gsm_voice */, "modify voice connection state",
           "the 'gsm voice <state>' allows you to change the state of your GPRS connection\r\n"
           "valid values for <state> are the following:\r\n\r\n"
           "  unregistered    no network available\r\n"
           "  home            on local network, non-roaming\r\n"
           "  roaming         on roaming network\r\n"
           "  searching       searching networks\r\n"
           "  denied          emergency calls only\r\n"
           "  off             same as 'unregistered'\r\n"
           "  on              same as 'home'\r\n",
           [](ConsoleContext& ctx, const std::string& state) {
               auto cell_status_or = ParseCellStatus(state);
               if (!cell_status_or.ok()) {
                   return absl::InvalidArgumentError(
                           "bad GSM data state name, try 'help gsm voice' for list of valid "
                           "values");
               }
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               CellInfo request;
               request.set_cell_status_voice(*cell_status_or);
               CellInfo response;
               return GrpcStatusToAbslStatus(stub->setCellInfo(context.get(), request, &response));
           });

    gsm.On("status" /* do_gsm_status */, "display GSM status",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               google::protobuf::Empty request;
               CellInfo response;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       stub->getCellInfo(context.get(), request, &response)));
               return absl::StrCat(
                       "gsm voice state: ", CellStatusToString(response.cell_status_voice()),
                       "\r\n",
                       "gsm data state:  ", CellStatusToString(response.cell_status_data()));
           });

    gsm.On("signal" /* do_gsm_signal */, "set sets the rssi and ber",
           [](ConsoleContext& ctx, int rssi, std::optional<int> ber) {
               if ((rssi < 0 || rssi > 31) && rssi != 99) {
                   return absl::InvalidArgumentError("invalid RSSI - must be 0..31 or 99");
               }
               if (ber.has_value() && (*ber < 0 || *ber > 7) && *ber != 99) {
                   return absl::InvalidArgumentError("invalid BER - must be 0..7 or 99");
               }
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               CellInfo request;
               request.mutable_cell_signal_strength()->set_rssi(rssi);
               CellInfo response;
               return GrpcStatusToAbslStatus(stub->setCellInfo(context.get(), request, &response));
           });

    gsm.On("signal-profile" /* do_gsm_signal_profile */, "set the signal strength profile",
           [](ConsoleContext& ctx, int strength) {
               if (strength < 0 || strength > 4) {
                   return absl::InvalidArgumentError("invalid signal strength - must be 0..4");
               }
               ASSIGN_OR_RETURN(auto stub, ctx.ModemStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               CellInfo request;
               request.mutable_cell_signal_strength()->set_level(
                       static_cast<CellSignalStrength::CellSignalLevel>(strength));
               CellInfo response;
               return GrpcStatusToAbslStatus(stub->setCellInfo(context.get(), request, &response));
           });
}

}  // namespace goldfish::telnet
