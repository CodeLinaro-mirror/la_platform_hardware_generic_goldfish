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

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace goldfish::parsing {

/**
 * @brief Helper to consume and parse command arguments with type safety.
 *
 * This class provides a stream-oriented interface for parsing command-line
 * strings. It is specifically designed for telnet-based consoles, offering
 * a balance between Bash-like flexibility and the robustness required for
 * network-based input.
 *
 * ### Parsing Rules
 * 1. **Word Splitting:** Arguments are separated by one or more whitespace
 *    characters (as defined by `absl::ascii_isspace`).
 * 2. **Quote Stripping:** Both single (`'`) and double (`"`) quotes can be
 *    used to protect whitespace. These quotes are stripped from the final
 *    argument string.
 * 3. **Mid-Token Quoting:** Quotes do not need to wrap the entire argument.
 *    For example, `key="value with spaces"` is parsed as a single token:
 *    `key=value with spaces`.
 * 4. **Escaping:** The backslash (`\`) escapes the literal value of the
 *    immediately following character, even inside quoted sections.
 *    This allows for `\"` or `\'` to be included as literal quotes.
 * 5. **Concatenation:** Multiple quoted and unquoted segments appearing
 *    without whitespace are concatenated into a single argument.
 *    For example, `file" name".txt` becomes `file name.txt`.
 *
 * ### Type-Safe Extraction
 * The methods `NextInt()`, `NextDouble()`, and `NextBool()` are non-destructive
 * on failure. If the next token cannot be parsed into the requested type,
 * the token remains in the stream, allowing the caller to attempt a
 * different parsing strategy or log an error without losing the data.
 *
 * Example usage:
 * @code
 *   ArgStream args("command 123 true hello\ world");
 *   auto cmd = args.Next(); // "command"
 *
 *   auto count = args.NextInt();
 *   if (!count.ok()) return count.status();
 *   // *count is 123
 *
 *   auto flag = args.NextBool();
 *   if (!flag.ok()) return flag.status();
 *   // *flag is true
 *
 *   auto s = args.Next(); // "hello world"
 * @endcode
 */
class ArgStream {
  public:
    explicit ArgStream(std::string line);
    ArgStream(const ArgStream&) = delete;
    ArgStream& operator=(const ArgStream&) = delete;

    /**
     * @brief Consumes and returns the next argument.
     *
     * @return The next argument as a string, or an empty string if
     * there are no more arguments.
     */
    std::string Next();

    /**
     * @brief Returns the next argument without consuming it.
     *
     * @return The next argument as a string, or an empty string if
     * there are no more arguments.
     */
    std::string Peek() const;

    /**
     * @brief Checks if there are no more arguments to consume.
     *
     * @return true if the stream is empty, false otherwise.
     */
    bool Empty() const;

    /**
     * @brief Returns the raw remainder of the command line.
     *
     * This returns the rest of the command line string starting from the
     * current position, preserving whitespace and quoting.
     *
     * @return A view containing the remaining arguments.
     */
    std::string_view Remaining() const;

    /**
     * @brief Returns the original full command line string.
     */
    const std::string& Line() const { return line_; }

    /**
     * @brief Consumes the next argument and parses it as an integer.
     *
     * @return The parsed integer on success, or an error status if parsing fails.
     */
    absl::StatusOr<int> NextInt();

    /**
     * @brief Consumes the next argument and parses it as a double.
     *
     * @return The parsed double on success, or an error status if parsing fails.
     */
    absl::StatusOr<double> NextDouble();

    /**
     * @brief Consumes the next argument and parses it as a boolean.
     *
     * Accepts the following values (case-insensitive):
     * - True: "on", "true", "yes", "1"
     * - False: "off", "false", "no", "0"
     *
     * @return The parsed boolean on success, or an error status if parsing fails.
     */
    absl::StatusOr<bool> NextBool();

  private:
    struct ParseResult {
        size_t index;       ///< Next index if this token is consumed
        std::string value;  ///< The actual value
    };
    ParseResult ParseEntry() const;
    void Consume();
    void Clear();

    // class invariant !std::isspace(line_[index_])
    const std::string line_;      ///< The line to be parsed
    size_t index_ = 0;            ///< Current index of token to parse
    mutable ParseResult peeked_;  ///< Cached peeked token
};

}  // namespace goldfish::parsing
