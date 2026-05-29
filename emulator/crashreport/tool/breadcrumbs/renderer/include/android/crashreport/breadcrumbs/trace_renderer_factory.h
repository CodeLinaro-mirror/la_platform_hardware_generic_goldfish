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

#include <cstdint>
#include <memory>

#include "android/crashreport/breadcrumbs/trace_renderer.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Centralized factory for renderer instantiation.
 */
class TraceRendererFactory {
  public:
    /**
     * @brief Supported rendering formats for the forensic report.
     */
    enum class RenderFormat : uint8_t {
        kText,     ///< High-density ANSI colored timeline for CLI.
        kMermaid,  ///< Markdown-compatible Mermaid.js sequence diagram.
    };

    /**
     * @brief Creates the appropriate renderer for the given format.
     *
     * @param format The requested output format.
     * @param use_color Preference for ANSI coloring (only applicable to TEXT).
     * @return A unique pointer to the instantiated renderer.
     */
    static std::unique_ptr<TraceRenderer> Create(RenderFormat format, bool use_color = true);
};

}  // namespace android::crashreport::breadcrumbs
