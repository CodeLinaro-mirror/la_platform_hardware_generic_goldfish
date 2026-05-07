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
#pragma once

#include <string>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Strategy interface for rendering DiagnosticTrace models.
 */
class TraceRenderer {
  public:
    virtual ~TraceRenderer() = default;

    /**
     * @brief Renders the structured trace into a string representation.
     *
     * @param trace The forensic model to render.
     * @return A formatted string containing the report.
     */
    virtual std::string Render(const DiagnosticTrace& trace) const = 0;
};

}  // namespace android::crashreport::breadcrumbs
