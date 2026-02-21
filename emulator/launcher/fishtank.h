// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/cmdline_option.h"
#include "goldfish/async/launch_config.h"

namespace goldfish::launcher::fishtank {

absl::StatusOr<::goldfish::async::LaunchConfig> launch_config(
        const std::filesystem::path& fishtank_binary, const std::string& avd_name, int serial_number,
        const AndroidOptions& opts) {
    std::vector<std::string> args;
    args.push_back(absl::StrCat("@", avd_name));
    args.push_back("-fishtank");
    args.push_back(absl::StrCat(serial_number));
    args.push_back("-verbose");
    if (opts.qt_hide_window) {
        args.push_back("-qt-hide-window");
    }

    LOG(INFO) << "Fishtank launch command: " << fishtank_binary << " " << absl::StrJoin(args, " ");
    return ::goldfish::async::LaunchConfig{
        .exe_path = fishtank_binary,
        .args = args,
        //.environment = {},
        .daemon = true,
        .keep_stdio = opts.fishtank_stdout != 0,
    };
}

}  // namespace goldfish::launcher::fishtank