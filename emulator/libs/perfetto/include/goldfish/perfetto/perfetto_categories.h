// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "perfetto/tracing/track_event.h"

/**
 * @brief Defines Perfetto tracing categories for the goldfish emulator.
 *
 * This file contains the definitions of categories used for track events.
 * To use these categories in your code, include this header and use the
 * standard Perfetto macros such as TRACE_EVENT, TRACE_EVENT_BEGIN, etc.
 *
 * More information on the macros can be found in the Perfetto
 * documentation: https://perfetto.dev/docs/instrumentation/track-events
 *
 * Example usage:
 * @code
 * #include "goldfish/perfetto/perfetto_categories.h"
 *
 * void MyFunction() {
 *     TRACE_EVENT("rendering", "MyFunction");
 *     // ... do work ...
 * }
 * @endcode
 *
 * To add your own category:
 * 1. Add a new `perfetto::Category("your_category_name")` to the list below.
 * 2. Provide a description.
 * 3. Add it to the enabled categories in perfetto_config.cc if you want it enabled by default.
 *
 * Example of adding a category:
 * @code
 *     perfetto::Category("my_new_category")
 *         .SetDescription("Description of my new category"),
 * @endcode
 */
PERFETTO_DEFINE_CATEGORIES(
        perfetto::Category("rendering").SetDescription("Rendering and graphics events"),
        perfetto::Category("async").SetDescription(
                "Event loop primitives, task scheduling, and I/O"));
