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
#include "emulator/crashreport/tool/minidump_processor.h"

#include <limits>

#include "absl/log/log.h"
#include "absl/memory/memory.h"

#include "google_breakpad/processor/minidump.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "processor/simple_symbol_supplier.h"

namespace android {
namespace crashreport {

bool MinidumpProcessor::Process(const std::string& minidump_file,
                                const std::vector<std::string>& symbol_paths,
                                google_breakpad::ProcessState* process_state,
                                google_breakpad::BasicSourceLineResolver* resolver) {
    std::unique_ptr<google_breakpad::SimpleSymbolSupplier> symbol_supplier;
    if (!symbol_paths.empty()) {
        symbol_supplier = std::make_unique<google_breakpad::SimpleSymbolSupplier>(symbol_paths);
    }

    google_breakpad::MinidumpProcessor minidump_processor(symbol_supplier.get(), resolver);

    google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
    google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());

    google_breakpad::Minidump dump(minidump_file);
    if (!dump.Read()) {
        LOG(ERROR) << "Minidump " << dump.path() << " could not be read";
        return false;
    }

    if (minidump_processor.Process(&dump, process_state) != google_breakpad::PROCESS_OK) {
        LOG(ERROR) << "MinidumpProcessor::Process failed";
        return false;
    }
    return true;
}

}  // namespace crashreport
}  // namespace android
