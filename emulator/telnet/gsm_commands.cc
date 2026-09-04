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
#include "netsim/cell.grpc.pb.h"
#include "netsim/common.pb.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;

namespace {

struct NetsimEnv {
    std::unique_ptr<::netsim::cell::CellService::StubInterface> stub;
    uint32_t chip_id;
    std::unique_ptr<::grpc::ClientContext> context;
};

absl::StatusOr<NetsimEnv> GetNetsimEnv(ConsoleContext& ctx) {
    ASSIGN_OR_RETURN(auto stub, ctx.NetsimCellStub());
    ASSIGN_OR_RETURN(auto chip_id, ctx.GetCellularChipId());
    ASSIGN_OR_RETURN(auto context, ctx.NewNetsimContext());
    return NetsimEnv{std::move(stub), chip_id, std::move(context)};
}

bool CheckPhoneNumber(std::string_view number) {
    if (number.empty()) return false;
    for (char c : number) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '+' && c != '#' && c != '*') {
            return false;
        }
    }
    return true;
}

const char* NetsimCellStatusToString(netsim::cell::RegistrationStatus status) {
    switch (status) {
    case netsim::cell::RegistrationStatus::NOT_REGISTERED:
        return "unregistered";
    case netsim::cell::RegistrationStatus::REGISTERED_HOME:
        return "home";
    case netsim::cell::RegistrationStatus::ROAMING:
        return "roaming";
    case netsim::cell::RegistrationStatus::SEARCHING:
        return "searching";
    case netsim::cell::RegistrationStatus::DENIED:
        return "denied";
    default:
        return "<unknown>";
    }
}

absl::StatusOr<netsim::cell::RegistrationStatus> ParseNetsimCellStatus(std::string_view state) {
    if (state == "unregistered" || state == "off")
        return netsim::cell::RegistrationStatus::NOT_REGISTERED;
    if (state == "home" || state == "on") return netsim::cell::RegistrationStatus::REGISTERED_HOME;
    if (state == "roaming") return netsim::cell::RegistrationStatus::ROAMING;
    if (state == "searching") return netsim::cell::RegistrationStatus::SEARCHING;
    if (state == "denied") return netsim::cell::RegistrationStatus::DENIED;
    return absl::InvalidArgumentError(
            "bad GSM data state name, try 'help gsm data' for list of valid values");
}

const char* NetsimCallStateToString(netsim::cell::Call::State state) {
    switch (state) {
    case netsim::cell::Call::ACTIVE:
        return "active";
    case netsim::cell::Call::HOLDING:
        return "held";
    case netsim::cell::Call::ALERTING:
        return "ringing";
    case netsim::cell::Call::WAITING:
        return "waiting";
    case netsim::cell::Call::INCOMING:
        return "incoming";
    case netsim::cell::Call::DIALING:
        return "dialing";
    default:
        return "unknown";
    }
}

}  // namespace

void RegisterGsmCommands(CommandRegistryBuilder::NodeBuilder& gsm) {
    auto update_call_state = [](ConsoleContext& ctx, const std::string& remote_number,
                                netsim::cell::Call::State state, const char* error_msg) {
        ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));

        netsim::cell::GetCellRequest get_req;
        get_req.set_id(env.chip_id);
        netsim::cell::Cell cell_info;
        RETURN_IF_ERROR(
                GrpcStatusToAbslStatus(env.stub->Get(env.context.get(), get_req, &cell_info)));

        bool found = false;
        for (const auto& call : cell_info.active_calls()) {
            if (call.number() == remote_number) {
                found = true;
                break;
            }
        }
        if (!found) {
            return absl::FailedPreconditionError(
                    absl::StrFormat("no current call to/from number '%s'", remote_number));
        }

        ASSIGN_OR_RETURN(auto context_upd, ctx.NewNetsimContext());
        netsim::cell::ExecuteCellRequest request;
        request.set_id(env.chip_id);

        if (state == netsim::cell::Call::HOLDING) {
            request.mutable_remote_hold()->set_on_hold(true);
        } else if (state == netsim::cell::Call::ACTIVE) {
            netsim::cell::Call::State current_state = netsim::cell::Call::UNKNOWN;
            for (const auto& call : cell_info.active_calls()) {
                if (call.number() == remote_number) {
                    current_state = call.state();
                    break;
                }
            }
            if (current_state == netsim::cell::Call::HOLDING) {
                request.mutable_remote_hold()->set_on_hold(false);
            } else {
                request.mutable_remote_answer();
            }
        } else {
            return absl::InvalidArgumentError("Unsupported state transition in netsim");
        }

        google::protobuf::Empty response;
        auto status = env.stub->Execute(context_upd.get(), request, &response);
        if (!status.ok()) {
            return absl::FailedPreconditionError(error_msg);
        }
        return absl::OkStatus();
    };

    gsm.On("list" /* do_gsm_list */, "list current phone calls",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::GetCellRequest request;
               request.set_id(env.chip_id);
               netsim::cell::Cell response;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       env.stub->Get(env.context.get(), request, &response)));
               std::string result;
               for (const auto& call : response.active_calls()) {
                   std::string dir = "call";
                   if (call.direction() == netsim::cell::Call::MOBILE_ORIGINATED) {
                       dir = "outbound to ";
                   } else if (call.direction() == netsim::cell::Call::MOBILE_TERMINATED) {
                       dir = "inbound from";
                   }
                   if (!result.empty()) {
                       result += "\r\n";
                   }
                   absl::StrAppend(&result, absl::StrFormat("%s %-10s : %s", dir, call.number(),
                                                            NetsimCallStateToString(call.state())));
               }
               return result;
           });

    gsm.On("call" /* do_gsm_call */, "create inbound phone call",
           [](ConsoleContext& ctx, const std::string& phonenumber) {
               if (!CheckPhoneNumber(phonenumber)) {
                   return absl::InvalidArgumentError(
                           "bad phone number format, use digits, # and + only");
               }
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_incoming_call()->set_number(phonenumber);
               google::protobuf::Empty response;
               return GrpcStatusToAbslStatus(
                       env.stub->Execute(env.context.get(), request, &response));
           });

    gsm.On("busy" /* do_gsm_busy */, "close waiting outbound call as busy",
           [](ConsoleContext& ctx, const std::string& remote_number) {
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::GetCellRequest get_req;
               get_req.set_id(env.chip_id);
               netsim::cell::Cell cell_info;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       env.stub->Get(env.context.get(), get_req, &cell_info)));

               bool found_outbound = false;
               for (const auto& call : cell_info.active_calls()) {
                   if (call.number() == remote_number &&
                       call.direction() == netsim::cell::Call::MOBILE_ORIGINATED &&
                       (call.state() == netsim::cell::Call::DIALING ||
                        call.state() == netsim::cell::Call::ALERTING)) {
                       found_outbound = true;
                       break;
                   }
               }
               if (!found_outbound) {
                   return absl::FailedPreconditionError(absl::StrFormat(
                           "no current outbound call to number '%s' (call 0x0)", remote_number));
               }

               ASSIGN_OR_RETURN(auto context_del, ctx.NewNetsimContext());
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_end_call();
               google::protobuf::Empty response;
               auto status = env.stub->Execute(context_del.get(), request, &response);
               if (!status.ok()) {
                   return absl::FailedPreconditionError("could not set this number as busy");
               }
               return absl::OkStatus();
           });

    gsm.On("hold" /* do_gsm_hold */, "change the state of an active call to 'held'",
           [update_call_state](ConsoleContext& ctx, const std::string& remote_number) {
               return update_call_state(ctx, remote_number, netsim::cell::Call::HOLDING,
                                        "could not put this call on hold");
           });

    gsm.On("accept" /* do_gsm_accept */, "change the state of an incoming or held call to 'active'",
           [update_call_state](ConsoleContext& ctx, const std::string& remote_number) {
               return update_call_state(ctx, remote_number, netsim::cell::Call::ACTIVE,
                                        "could not activate this call");
           });

    gsm.On("cancel" /* do_gsm_cancel */, "disconnect an inbound or outbound phone call",
           [](ConsoleContext& ctx, const std::string& remote_number) {
               if (!CheckPhoneNumber(remote_number)) {
                   return absl::InvalidArgumentError(
                           "bad phone number format, use digits, # and + only");
               }
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));

               netsim::cell::GetCellRequest get_req;
               get_req.set_id(env.chip_id);
               netsim::cell::Cell cell_info;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       env.stub->Get(env.context.get(), get_req, &cell_info)));

               bool found = false;
               for (const auto& call : cell_info.active_calls()) {
                   if (call.number() == remote_number) {
                       found = true;
                       break;
                   }
               }
               if (!found) {
                   return absl::FailedPreconditionError(
                           absl::StrFormat("no current call to/from number '%s'", remote_number));
               }

               ASSIGN_OR_RETURN(auto context_del, ctx.NewNetsimContext());
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_end_call();
               google::protobuf::Empty response;
               auto status = env.stub->Execute(context_del.get(), request, &response);
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
               ASSIGN_OR_RETURN(auto cell_status, ParseNetsimCellStatus(state));
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_set_data_registration()->set_status(cell_status);
               google::protobuf::Empty response;
               return GrpcStatusToAbslStatus(
                       env.stub->Execute(env.context.get(), request, &response));
           });

    gsm.On("meter" /* do_gsm_meter */, "modify mobile data meterness",
           "the 'gsm meter <on|off>' allows you to change the meterness of your mobile data "
           "plan\r\n",
           [](ConsoleContext& ctx, bool metered) {
               // Metered status was a no-op in the legacy modem simulator and is
               // unsupported by Netsim. We explicitly return UnimplementedError.
               return absl::UnimplementedError(
                       "Metered status is not supported by netsim cellular simulation");
           });

    gsm.On("voice" /* do_gsm_voice */, "modify voice connection state",
           "the 'gsm voice <state>' allows you to change the state of your voice connection\r\n"
           "valid values for <state> are the following:\r\n\r\n"
           "  unregistered    no network available\r\n"
           "  home            on local network, non-roaming\r\n"
           "  roaming         on roaming network\r\n"
           "  searching       searching networks\r\n"
           "  denied          emergency calls only\r\n"
           "  off             same as 'unregistered'\r\n"
           "  on              same as 'home'\r\n",
           [](ConsoleContext& ctx, const std::string& state) {
               auto cell_status_or = ParseNetsimCellStatus(state);
               if (!cell_status_or.ok()) {
                   return absl::InvalidArgumentError(
                           "bad GSM voice state name, try 'help gsm voice' for list of valid "
                           "values");
               }
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_set_voice_registration()->set_status(*cell_status_or);
               google::protobuf::Empty response;
               return GrpcStatusToAbslStatus(
                       env.stub->Execute(env.context.get(), request, &response));
           });

    gsm.On("status" /* do_gsm_status */, "display GSM status",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::GetCellRequest request;
               request.set_id(env.chip_id);
               netsim::cell::Cell response;
               RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                       env.stub->Get(env.context.get(), request, &response)));
               return absl::StrCat(
                       "gsm voice state: ", NetsimCellStatusToString(response.voice_registration()),
                       "\r\n",
                       "gsm data state:  ", NetsimCellStatusToString(response.data_registration()));
           });

    gsm.On("signal" /* do_gsm_signal */, "set the rssi and ber",
           [](ConsoleContext& ctx, int rssi, std::optional<int> ber) {
               if ((rssi < 0 || rssi > 31) && rssi != 99) {
                   return absl::InvalidArgumentError("invalid RSSI - must be 0..31 or 99");
               }
               if (ber.has_value() && (*ber < 0 || *ber > 7) && *ber != 99) {
                   return absl::InvalidArgumentError("invalid BER - must be 0..7 or 99");
               }
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);
               request.mutable_set_signal_strength()->set_rssi(rssi);
               if (ber.has_value()) {
                   request.mutable_set_signal_strength()->set_ber(*ber);
               }
               google::protobuf::Empty response;
               return GrpcStatusToAbslStatus(
                       env.stub->Execute(env.context.get(), request, &response));
           });

    gsm.On("signal-profile" /* do_gsm_signal_profile */, "set the signal strength profile",
           [](ConsoleContext& ctx, int strength) {
               if (strength < 0 || strength > 4) {
                   return absl::InvalidArgumentError("invalid signal strength - must be 0..4");
               }
               ASSIGN_OR_RETURN(auto env, GetNetsimEnv(ctx));
               netsim::cell::ExecuteCellRequest request;
               request.set_id(env.chip_id);

               // Map abstract console signal levels (0..4) to TS 27.007 RSSI indices (0..31)
               // because Netsim operates on standard RSSI rather than abstract levels.
               // The mapping aligns with Android's default signal strength bar thresholds:
               // Level 0: RSSI 0   (-113 dBm, < -107 dBm threshold for 0 bars)
               // Level 1: RSSI 5   (-103 dBm, >= -107 dBm threshold for 1 bar)
               // Level 2: RSSI 10  (-93 dBm,  >= -97 dBm  threshold for 2 bars)
               // Level 3: RSSI 15  (-83 dBm,  >= -87 dBm  threshold for 3 bars)
               // Level 4: RSSI 31  (-51 dBm,  >= -77 dBm  threshold for 4 bars)
               int rssi = 99;
               switch (strength) {
               case 0:
                   rssi = 0;
                   break;
               case 1:
                   rssi = 5;
                   break;
               case 2:
                   rssi = 10;
                   break;
               case 3:
                   rssi = 15;
                   break;
               case 4:
                   rssi = 31;
                   break;
               default:
                   return absl::InvalidArgumentError("invalid signal strength - must be 0..4");
               }

               request.mutable_set_signal_strength()->set_rssi(rssi);
               google::protobuf::Empty response;
               return GrpcStatusToAbslStatus(
                       env.stub->Execute(env.context.get(), request, &response));
           });
}

}  // namespace goldfish::telnet
