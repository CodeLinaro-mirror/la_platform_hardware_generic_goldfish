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
#include "android/crashreport/breadcrumbs/trace_renderer_factory.h"

#include "android/crashreport/breadcrumbs/ansi_renderer.h"
#include "android/crashreport/breadcrumbs/mermaid_renderer.h"

namespace android::crashreport::breadcrumbs {

std::unique_ptr<TraceRenderer> TraceRendererFactory::Create(RenderFormat format, bool use_color) {
    switch (format) {
    case RenderFormat::kMermaid:
        return std::make_unique<MermaidRenderer>();
    case RenderFormat::kText:
    default:
        return std::make_unique<AnsiRenderer>(use_color);
    }
}

}  // namespace android::crashreport::breadcrumbs
