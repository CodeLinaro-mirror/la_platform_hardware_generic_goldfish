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

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "console_context.h"

namespace goldfish::telnet {

namespace {

constexpr std::string_view kGsmStateValuesHelp =
        "valid values for <state> are the following:\r\n\r\n"
        "  unregistered    no network available\r\n"
        "  home            on local network, non-roaming\r\n"
        "  roaming         on roaming network\r\n"
        "  searching       searching networks\r\n"
        "  denied          emergency calls only\r\n"
        "  off             same as 'unregistered'\r\n"
        "  on              same as 'home'\r\n";

absl::Status GsmDeprecatedError() {
    return absl::UnimplementedError(kGsmDeprecatedMessage);
}

}  // namespace

void RegisterGsmCommands(CommandRegistryBuilder::NodeBuilder& gsm) {
    const auto deprecated_no_args = [](ConsoleContext& /*ctx*/) -> absl::StatusOr<std::string> {
        return GsmDeprecatedError();
    };
    const auto deprecated_str_arg = [](ConsoleContext& /*ctx*/, const std::string& /*arg*/) {
        return GsmDeprecatedError();
    };

    gsm.On("list" /* do_gsm_list */, "list current phone calls", deprecated_no_args);

    for (const auto& [cmd, abstract] : std::initializer_list<std::pair<const char*, const char*>>{
                 {"call" /* do_gsm_call */, "create inbound phone call"},
                 {"busy" /* do_gsm_busy */, "close waiting outbound call as busy"},
                 {"hold" /* do_gsm_hold */, "change the state of an active call to 'held'"},
                 {"accept" /* do_gsm_accept */,
                  "change the state of an incoming or held call to 'active'"},
                 {"cancel" /* do_gsm_cancel */, "disconnect an inbound or outbound phone call"},
         }) {
        gsm.On(cmd, abstract, deprecated_str_arg);
    }

    gsm.On("data" /* do_gsm_data */, "modify data connection state",
           absl::StrCat("the 'gsm data <state>' allows you to change the state of your GPRS "
                        "connection\r\n",
                        kGsmStateValuesHelp),
           deprecated_str_arg);

    gsm.On("meter" /* do_gsm_meter */, "modify mobile data meterness",
           "the 'gsm meter <on|off>' allows you to change the meterness of your mobile data "
           "plan\r\n",
           [](ConsoleContext& /*ctx*/, bool /*metered*/) { return GsmDeprecatedError(); });

    gsm.On("voice" /* do_gsm_voice */, "modify voice connection state",
           absl::StrCat("the 'gsm voice <state>' allows you to change the state of your voice "
                        "connection\r\n",
                        kGsmStateValuesHelp),
           deprecated_str_arg);

    gsm.On("status" /* do_gsm_status */, "display GSM status", deprecated_no_args);

    gsm.On("signal" /* do_gsm_signal */, "set the rssi and ber",
           [](ConsoleContext& /*ctx*/, int /*rssi*/, std::optional<int> /*ber*/) {
               return GsmDeprecatedError();
           });

    gsm.On("signal-profile" /* do_gsm_signal_profile */, "set the signal strength profile",
           [](ConsoleContext& /*ctx*/, int /*strength*/) { return GsmDeprecatedError(); });
}

}  // namespace goldfish::telnet
