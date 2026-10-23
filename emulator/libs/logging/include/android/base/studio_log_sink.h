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

#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/log/log_entry.h"
#include "android/base/color_log_sink.h"

namespace android::base {

// Formats log messages with Android Studio's console prefixes:
//   USER_INFO    | <message>
//   USER_WARNING | <message>
//   USER_ERROR   | <message>
//   FATAL        | <message>
//
// ANSI colors and verbose timestamps/source locations are never enabled so
// Android Studio's console parser always sees the exact 12-character label
// followed by " | ".
class StudioLogSink : public ColorLogSink {
  public:
    StudioLogSink(std::ostream* stream, std::string label)
            : ColorLogSink(stream, /*use_color=*/false), label_(std::move(label)) {}
    ~StudioLogSink() override = default;

    std::string_view TranslateSeverity(const absl::LogEntry& entry) const override;

  private:
    std::string label_;
};

StudioLogSink* StudioInfoSink();
StudioLogSink* StudioWarningSink();
StudioLogSink* StudioErrorSink();
StudioLogSink* StudioFatalSink();

// Emits "FATAL        | <message>" via StudioFatalSink() and terminates the
// process with exit code 1 via ABSL_LOG(QFATAL) (_exit(1)) without triggering
// Crashpad or signal handlers. Call only after stack RAII guards have unwound.
[[noreturn]] void ExitWithStudioFatalMessage(std::string_view message);

// Redirects all static StudioLogSink instances to |stream| (pass &std::cout to
// restore default behavior). For unit tests only.
void SetStudioLogStreamForTesting(std::ostream* stream);

}  // namespace android::base

// USER_MESSAGE(INFO) intentionally dispatches at absl::LogSeverity::kWarning so
// that messages are emitted and flushed even when -verbose is off (where the
// global Abseil MinLogLevel is kWarning), while StudioInfoSink() formats the
// prefix as "USER_INFO    | " and ToSinkOnly prevents global sink duplication.
#define STUDIO_LOG_INTERNAL_DISPATCH_INFO \
    ABSL_LOG(WARNING).ToSinkOnly(::android::base::StudioInfoSink())
#define STUDIO_LOG_INTERNAL_DISPATCH_WARNING \
    ABSL_LOG(WARNING).ToSinkOnly(::android::base::StudioWarningSink())
#define STUDIO_LOG_INTERNAL_DISPATCH_ERROR \
    ABSL_LOG(ERROR).ToSinkOnly(::android::base::StudioErrorSink())

#define USER_MESSAGE(severity) STUDIO_LOG_INTERNAL_DISPATCH_##severity
