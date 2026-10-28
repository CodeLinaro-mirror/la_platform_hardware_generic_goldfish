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

#include "android/base/studio_log_sink.h"

#include <iostream>
#include <string_view>

#include "absl/base/no_destructor.h"

namespace android::base {

std::string_view StudioLogSink::TranslateSeverity(const absl::LogEntry& /*entry*/) const {
    return label_;
}

StudioLogSink* StudioInfoSink() {
    static absl::NoDestructor<StudioLogSink> sink(&std::cout, "USER_INFO   ");
    return sink.get();
}

StudioLogSink* StudioWarningSink() {
    static absl::NoDestructor<StudioLogSink> sink(&std::cout, "USER_WARNING");
    return sink.get();
}

StudioLogSink* StudioErrorSink() {
    static absl::NoDestructor<StudioLogSink> sink(&std::cout, "USER_ERROR  ");
    return sink.get();
}

StudioLogSink* StudioFatalSink() {
    static absl::NoDestructor<StudioLogSink> sink(&std::cout, "FATAL       ");
    return sink.get();
}

[[noreturn]] void ExitWithStudioFatalMessage(std::string_view message) {
    ABSL_LOG(QFATAL).ToSinkOnly(StudioFatalSink()) << message;
}

void SetStudioLogStreamForTesting(std::ostream* stream) {
    StudioInfoSink()->SetOutputStream(stream);
    StudioWarningSink()->SetOutputStream(stream);
    StudioErrorSink()->SetOutputStream(stream);
    StudioFatalSink()->SetOutputStream(stream);
}

}  // namespace android::base
