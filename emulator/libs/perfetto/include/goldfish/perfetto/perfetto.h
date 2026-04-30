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

#include <filesystem>
#include <memory>
#include <string_view>

#include "perfetto/tracing/tracing.h"

/**
 * @file perfetto.h
 * @brief Provides a simplified interface for Perfetto tracing within the goldfish emulator.
 */

namespace goldfish::perfetto {

/**
 * @brief Initializes the Perfetto tracing system for the emulator.
 *
 * This function sets up the Perfetto in-process backend and registers the
 * required track events. It must be called once before any tracing sessions
 * are created, typically during emulator startup.
 */
void Initialize();

/**
 * @brief Manages a Perfetto tracing session using RAII.
 *
 * This class provides a convenient way to start and stop a tracing session.
 * The session is started in the constructor and stopped either by calling Stop()
 * or automatically upon destruction. It uses the in-process backend to record
 * trace data directly to a file.
 */
class EmulatorTracingSession {
  public:
    /**
     * @brief Constructs and starts a new Perfetto tracing session.
     *
     * Opens the specified trace file and initializes a blocking tracing session.
     * The session is configured with:
     * - A 32MB internal buffer.
     * - A 1-second periodic flush to disk.
     * - All categories disabled by default, except those explicitly listed.
     *
     * @param trace_file The filesystem path where the trace data will be written.
     * @param enabled_categories A comma-separated list of Perfetto categories to enable.
     *                           Example: "memory,gfx,input".
     */
    EmulatorTracingSession(const std::filesystem::path& trace_file,
                           std::string_view enabled_categories);

    /**
     * @brief Destructor. Automatically stops the tracing session if it's still running.
     */
    ~EmulatorTracingSession();

    // Disallow copy to prevent multiple sessions managing the same resource.
    EmulatorTracingSession(const EmulatorTracingSession&) = delete;
    EmulatorTracingSession& operator=(const EmulatorTracingSession&) = delete;

    // Allow move to support transferring ownership of a tracing session.
    EmulatorTracingSession(EmulatorTracingSession&&) = default;
    EmulatorTracingSession& operator=(EmulatorTracingSession&& other) noexcept = default;

    /**
     * @brief Manually stops the tracing session and finalizes the trace file.
     *
     * This method:
     * 1. Flushes all pending track events.
     * 2. Blocks until the Perfetto session has completely stopped.
     * 3. Resets the internal session object and closes the trace file.
     *
     * This method is idempotent; calling it multiple times has no effect after the first call.
     */
    void Stop();

  private:
    /// The underlying Perfetto tracing session.
    std::unique_ptr<::perfetto::TracingSession> session_;
};

}  // namespace goldfish::perfetto
