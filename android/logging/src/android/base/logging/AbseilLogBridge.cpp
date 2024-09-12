// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/base/logging/AbseilLogBridge.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"

/**
 * @brief Formats a string using vsnprintf and returns a std::string_view.
 *
 * This function formats a string using the provided format string and
 * arguments. If the formatted string exceeds the provided buffer size,
 * it is truncated and "..." is appended to indicate the truncation.
 *
 * @param buffer The buffer to write the formatted string to.
 * @param bufferSize The size of the buffer.
 * @param format The format string.
 * @param args The arguments to the format string.
 * @return A std::string_view of the formatted string.
 */
std::string_view formatString(char* buffer, int bufferSize, const char* format, va_list args) {
    int strlen = bufferSize;
    int size = vsnprintf(buffer, bufferSize, format, args);
    if (size >= bufferSize) {
        // Indicate trunctation.
        strncpy(buffer + bufferSize - 3, "...", 3);
    } else {
        strlen = size;
    }

    return std::string_view(buffer, strlen);
}

inline absl::LogSeverity severityToAbsl(int severity) {
    switch (severity) {
        case 0:
            return absl::LogSeverity::kInfo;
        case 1:
            return absl::LogSeverity::kWarning;
        case 2:
            return absl::LogSeverity::kError;
        case 3:
            return absl::LogSeverity::kFatal;
        default:
            return absl::LogSeverity::kInfo;
    }
}

/**
 * @brief Logs a message to Abseil logging library.
 *
 * This function takes a severity level, file name, line number, format string,
 * and variable arguments, formats the message, and logs it using the Abseil
 * logging library.
 *
 * Negative severity levels are treated as verbose logs.
 *
 * @param severity The severity level of the log message.
 * @param file The name of the file where the log message originated.
 * @param line The line number in the file where the log message originated.
 * @param format The format string for the log message.
 * @param ... The variable arguments for the format string.
 */
extern "C" void _log_to_abseil(int severity, const char* file, unsigned int line,
                               const char* format, ...) {
    constexpr int bufferSize = 4096;
    char buffer[bufferSize];
    static_assert(std::size(buffer) == bufferSize);

    va_list args;
    va_start(args, format);

    // Note that we will only format a string if the logging system
    // is enabled.
    if (severity < 0) {
        VLOG(abs(severity)).AtLocation(file, line)
                << formatString(buffer, std::size(buffer), format, args);
    } else {
        LOG(LEVEL(severityToAbsl(severity))).AtLocation(file, line)
                << formatString(buffer, std::size(buffer), format, args);
    }

    va_end(args);
}
