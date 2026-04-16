/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/metrics/text_metrics_writer.h"

#include <sstream>

#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace goldfish::metrics {

using ::testing::HasSubstr;

TEST(TextMetricsWriterTest, WriteEvent) {
    auto old_min_level = absl::MinLogLevel();
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kFatal);

    std::stringstream ss;
    TextMetricsWriter writer(ss);

    MetricsEvent event;
    event.as_event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
    event.as_event.set_studio_session_id("test-session");

    writer.Write(event);

    std::string output = ss.str();
    EXPECT_THAT(output, HasSubstr("event time"));
    EXPECT_THAT(output, HasSubstr("kind: EMULATOR_PING"));
    EXPECT_THAT(output, HasSubstr("studio_session_id: \"test-session\""));

    absl::SetMinLogLevel(old_min_level);
}

TEST(TextMetricsWriterTest, StreamOwnerWriteEvent) {
    auto ss = std::make_unique<std::stringstream>();
    auto* ss_ptr = ss.get();
    StreamOwnerTextMetricsWriter writer(std::move(ss));

    MetricsEvent event;
    event.as_event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
    event.as_event.set_studio_session_id("owned-session");

    writer.Write(event);

    std::string output = ss_ptr->str();
    EXPECT_THAT(output, HasSubstr("event time"));
    EXPECT_THAT(output, HasSubstr("kind: EMULATOR_PING"));
    EXPECT_THAT(output, HasSubstr("studio_session_id: \"owned-session\""));
}

}  // namespace goldfish::metrics
