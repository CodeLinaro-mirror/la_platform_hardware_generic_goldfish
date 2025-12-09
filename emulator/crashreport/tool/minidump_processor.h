// Copyright 2025 The Android Open Source Project
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
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/process_state.h"

namespace android {
namespace crashreport {

class MinidumpProcessor {
  public:
    MinidumpProcessor() = default;
    ~MinidumpProcessor() = default;

    bool Process(const std::string& minidump_file, const std::vector<std::string>& symbol_paths,
                 google_breakpad::ProcessState* process_state,
                 google_breakpad::BasicSourceLineResolver* resolver);
};

}  // namespace crashreport
}  // namespace android
