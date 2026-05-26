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
#include "goldfish/parsing/arg_stream.h"

// TODO: Consider adding support for configurable parsing styles (e.g., Strict Bash vs.
// Legacy Telnet). Current implementation is a "Best Effort" hybrid that strips
// quotes and handles mid-token quoting to satisfy modern CLI expectations while
// remaining compatible with legacy escaped strings.

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace goldfish::parsing {

ArgStream::ArgStream(std::string line) : line_(std::move(line)) {
    for (index_ = 0; index_ < line_.size() && absl::ascii_isspace(line_[index_]); index_++) {
        // skip space, to maintain class invariant: !absl::ascii_isspace(line_[index_])
    }
    Clear();
}

ArgStream::ParseResult ArgStream::ParseEntry() const {
    VLOG(1) << "ParseEntry: " << index_ << ", line_:[" << line_.substr(index_) << "]";
    if (index_ >= line_.size()) return {.index = index_, .value = ""};

    // Invariant first char != whitespace.
    DCHECK(!absl::ascii_isspace(line_[index_])) << "Entry parser started on whitespace!";
    constexpr char kQuote = '\'';
    constexpr char kDoubleQuote = '"';
    constexpr char kEscape = '\\';
    constexpr char kNone = '\000';

    bool escaped = false;
    char quoted = kNone;
    std::string value;
    size_t new_index = index_;

    for (; new_index < line_.size(); new_index++) {
        const char c = line_[new_index];

        // We have a \c, so take c and continue.
        if (escaped) {
            value.push_back(c);
            escaped = false;
            continue;
        }

        // we have a \, we will grab the next char if there's any left.
        if (c == kEscape) {
            if (new_index == line_.size() - 1) {
                // The end!
                value.push_back(c);
                continue;
            }
            escaped = true;
            continue;
        }

        // Did we reach a boundary?
        if (quoted == c) {
            quoted = kNone;
            continue;
        }

        if (quoted == kNone && absl::ascii_isspace(c)) {
            // A separator outside of a quote.
            new_index++;
            for (; new_index < line_.size() && absl::ascii_isspace(line_[new_index]); new_index++) {
                // Skip space, to maintain our invariant.
            }
            return {.index = new_index, .value = std::move(value)};
        }

        // Handle the case where a quote starts in the middle
        // ie my="value is here" --> my=value is here (bash style)
        if (quoted == kNone && c == kQuote) {
            quoted = kQuote;
            continue;
        }
        if (quoted == kNone && c == kDoubleQuote) {
            quoted = kDoubleQuote;
            continue;
        }

        // It's just a normal char.
        value.push_back(c);
    }

    // End of string..
    return {.index = new_index, .value = std::move(value)};
}

void ArgStream::Consume() {
    index_ = peeked_.index;
    Clear();
}

void ArgStream::Clear() {
    peeked_.index = index_ + 1;
    peeked_.value.clear();
}

std::string ArgStream::Next() {
    (void)Peek();
    auto value = std::move(peeked_.value);
    Consume();
    return value;
}

std::string ArgStream::Peek() const {
    if (index_ == peeked_.index) return peeked_.value;
    peeked_ = ParseEntry();
    VLOG(1) << "Peek: peeked_.index: " << peeked_.index << ", peeked_.value: " << peeked_.value;
    return peeked_.value;
}

bool ArgStream::Empty() const {
    return index_ >= line_.size();
}

std::string_view ArgStream::Remaining() const {
    return std::string_view(line_).substr(index_);
}

absl::StatusOr<int> ArgStream::NextInt() {
    const auto s = Peek();
    int val;
    if (absl::SimpleAtoi(s, &val)) {
        Consume();
        return val;
    }
    return absl::InvalidArgumentError(
            absl::StrCat("Invalid value: Expected an integer, but received '", s,
                         "'. "
                         "Please provide a valid numeric integer value."));
}

absl::StatusOr<double> ArgStream::NextDouble() {
    const auto s = Peek();
    double val;
    if (absl::SimpleAtod(s, &val)) {
        Consume();
        return val;
    }
    return absl::InvalidArgumentError(
            absl::StrCat("Invalid value: Expected a double, but received '", s,
                         "'. "
                         "Please provide a valid floating-point number."));
}

absl::StatusOr<bool> ArgStream::NextBool() {
    const auto s = Peek();
    if (absl::EqualsIgnoreCase(s, "on") || absl::EqualsIgnoreCase(s, "true") ||
        absl::EqualsIgnoreCase(s, "yes") || s == "1") {
        Consume();
        return true;
    }
    if (absl::EqualsIgnoreCase(s, "off") || absl::EqualsIgnoreCase(s, "false") ||
        absl::EqualsIgnoreCase(s, "no") || s == "0") {
        Consume();
        return false;
    }
    return absl::InvalidArgumentError(
            absl::StrCat("Invalid value: Expected a boolean (e.g., 'on', 'off', 'true', 'false', "
                         "'yes', 'no', '1', '0'), "
                         "but received '",
                         s, "'."));
}

}  // namespace goldfish::parsing
