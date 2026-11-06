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

#pragma once

#include <cstddef>
#include <string>

#include "goldfish/archive/writer.h"

namespace goldfish::archive {

// Appends serialized archive bytes to a std::string.
class StringWriter final : public IWriter {
  public:
    explicit StringWriter(std::string* out) : out_(out) {}

    void Write(const void* src, size_t size) override {
        if (size > 0) {
            out_->append(static_cast<const char*>(src), size);
        }
    }

  private:
    std::string* const out_;
};

}  // namespace goldfish::archive
