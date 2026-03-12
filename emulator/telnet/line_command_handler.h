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

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace goldfish::telnet {

/**
 * @brief Interface for handling lines received by a client connection to the emulator console.
 *
 * Implementations of this interface act as the logic engine for the console protocol.
 * They receive raw text input from the client and return a status-wrapped response
 * that determines the next step in the Telnet session.
 */
class LineCommandHandler {
  public:
    /**
     * @brief Persistent state maintained for the lifetime of a single client connection.
     */
    struct Context {
        virtual ~Context() = default;

        /**
         * @brief Indicates if the user has successfully performed authentication.
         *
         * Commands that are not explicitly marked as "Safe" will be rejected by
         * the registry if this is false.
         */
        bool authenticated = false;
    };

    virtual ~LineCommandHandler() = default;

    /**
     * @brief Processes a single command line and returns a protocol-aware result.
     *
     * The Registry uses the returned `absl::Status` to drive the Telnet state machine
     * and format the final response string sent to the client.
     *
     * ### Protocol Mapping Table
     *
     * | Status Code        | Logic Outcome     | Telnet Protocol Response          |
     * | :----------------- | :---------------- | :-------------------------------- |
     * | `absl::OkStatus()` | **Success**       | `[output]\r\nOK\r\n`              |
     * | `absl::IsAborted()`| **Exit Signal**   | *(None - Closes connection)*      |
     * | `Any Error`        | **Failure**       | `KO: [error_message]\r\n`         |
     *
     * @param line The raw, unparsed command line received from the Telnet client.
     * @param ctx  The mutable session context. Handlers can update this (e.g., to
     *             set `authenticated = true` after a successful token check).
     *
     * @return A `StatusOr` containing the handler's output message on success,
     *         an error status for failures, or `absl::AbortedError` to signal
     *         that the connection should be terminated immediately.
     */
    virtual absl::StatusOr<std::string> operator()(std::string line, Context& ctx) = 0;

    /**
     * @brief Optional message sent to a client immediately after they connect.
     *
     * @param ctx The current (usually initial) session context.
     * @return The string to be sent as the first message to the user.
     */
    virtual std::string WelcomeMessage(const Context& /*ctx*/) const { return ""; }
};

}  // namespace goldfish::telnet
