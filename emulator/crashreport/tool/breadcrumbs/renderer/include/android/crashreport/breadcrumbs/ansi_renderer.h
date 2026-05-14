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
#include "android/crashreport/breadcrumbs/trace_renderer.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Specialized renderer for high-contrast CLI visualization.
 *
 * The AnsiRenderer produces a structured table where events are ordered
 * chronologically. It uses ANSI escape codes to color-code gRPC calls,
 * highlight errors, and mark the crashing thread's "Last Breath".
 */
class AnsiRenderer : public TraceRenderer {
  public:
    /**
     * @brief Constructs an AnsiRenderer.
     *
     * @param use_color Whether to include ANSI escape codes for coloring.
     */
    explicit AnsiRenderer(bool use_color = true) : use_color_(use_color) {}

    /**
     * @brief Renders a structured trace into a color-coded or plain string.
     *
     * @param trace The forensic model to render.
     * @return A string containing the formatted table.
     */
    std::string Render(const DiagnosticTrace& trace) const override;

  private:
    bool use_color_;
};

}  // namespace android::crashreport::breadcrumbs
