// Copyright (C) 2025 The Android Open Source Project
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
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include "goldfish/singleton/application_singleton.h"

using namespace std::literals::string_view_literals;

const std::string application = "com.google.android.aquarium";
constexpr std::string_view kUsage = R"(
              .: .
            .    -            Welcome to the Aquarium!
        ==:    .-+       =-   The Android Emulator UI
     :+            :  #:  .   All your emulators are belong to us.
    %     @         :@-   -
   :              *=   - -    This application displays all running
   :  -<      :+++      -     Android Emulator instances.
    = _ _.*= .

)";

int aquarium_main(int argc, char* argv[]) {
    absl::SetProgramUsageMessage(kUsage);
    absl::InitializeLog();
    absl::ParseCommandLine(argc, argv);
    LOG(INFO) << "Aquarium is starting. Looking for running emulators.";

    goldfish::singleton::ApplicationSingleton singleton(application);

    if (!singleton.isPrimaryInstance()) {
        LOG(INFO) << "Another instance of the Aquarium is already running.";
        return 1;
    }
    return 0;
}