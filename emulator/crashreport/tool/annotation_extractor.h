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
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "util/file/file_reader.h"

namespace android {
namespace crashreport {

class AnnotationExtractor {
  public:
    nlohmann::json Extract(crashpad::FileReader* reader);
    std::vector<uint8_t> ExtractAnnotationBytes(crashpad::FileReader* reader,
                                                const std::string& name);
};

}  // namespace crashreport
}  // namespace android
