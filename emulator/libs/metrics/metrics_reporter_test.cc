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

#include "goldfish/metrics/metrics_reporter.h"

#include <gtest/gtest.h>

#include <vector>

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace goldfish::metrics {

class MockWriter : public MetricsWriter {
  public:
    void Write(MetricsEvent event) override {
        absl::MutexLock lock(mutex);
        events.push_back(std::move(event.as_event));
    }

    bool WaitForEvents(size_t count, absl::Duration timeout) {
        auto has_enough_events = [this, count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
            return events.size() >= count;
        };
        absl::MutexLock lock(mutex);
        return mutex.AwaitWithTimeout(absl::Condition(&has_enough_events), timeout);
    }

    std::vector<android_studio::AndroidStudioEvent> events;
    mutable absl::Mutex mutex;
};

TEST(MetricsReporterTest, ReportPing) {
    MetricsReporter reporter;
    auto writer = std::make_unique<MockWriter>();
    auto* writer_ptr = writer.get();
    reporter.SetWriter(std::move(writer));

    reporter.Report([](android_studio::AndroidStudioEvent& /*event*/) {
        // No modification needed, SetBaseFields will set kind and session_id.
    });

    ASSERT_TRUE(writer_ptr->WaitForEvents(1, absl::Seconds(5)));

    absl::MutexLock lock(writer_ptr->mutex);
    ASSERT_EQ(writer_ptr->events.size(), 1);
    EXPECT_EQ(writer_ptr->events[0].kind(), android_studio::AndroidStudioEvent::EMULATOR_PING);
    EXPECT_EQ(writer_ptr->events[0].studio_session_id(), reporter.session_id());
}

TEST(MetricsReporterTest, ReportMultiple) {
    MetricsReporter reporter;
    auto writer = std::make_unique<MockWriter>();
    auto* writer_ptr = writer.get();
    reporter.SetWriter(std::move(writer));

    for (int i = 0; i < 5; ++i) {
        reporter.Report([i](android_studio::AndroidStudioEvent& event) {
            event.set_kind(static_cast<android_studio::AndroidStudioEvent::EventKind>(i + 1));
        });
    }

    ASSERT_TRUE(writer_ptr->WaitForEvents(5, absl::Seconds(5)));

    absl::MutexLock lock(writer_ptr->mutex);
    ASSERT_EQ(writer_ptr->events.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(writer_ptr->events[i].kind(), i + 1);
    }
}

TEST(MetricsReporterTest, ReportWithCustomCallback) {
    MetricsReporter reporter;
    auto writer = std::make_unique<MockWriter>();
    auto* writer_ptr = writer.get();
    reporter.SetWriter(std::move(writer));

    reporter.Report([](android_studio::AndroidStudioEvent& event) {
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
        auto* details = event.mutable_emulator_details();
        details->set_core_version("custom-version");
    });

    ASSERT_TRUE(writer_ptr->WaitForEvents(1, absl::Seconds(5)));

    absl::MutexLock lock(writer_ptr->mutex);
    ASSERT_EQ(writer_ptr->events.size(), 1);
    EXPECT_EQ(writer_ptr->events[0].emulator_details().core_version(), "custom-version");
}

TEST(MetricsReporterTest, NoWriterNoCrash) {
    MetricsReporter reporter;
    // No writer set.
    reporter.Report([](android_studio::AndroidStudioEvent& /*event*/) {});
    // Should not crash.
}

TEST(MetricsReporterTest, SetWriter) {
    MetricsReporter reporter;

    auto writer1 = std::make_unique<MockWriter>();
    auto* writer1_ptr = writer1.get();
    reporter.SetWriter(std::move(writer1));

    reporter.Report([](android_studio::AndroidStudioEvent& event) {
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
    });
    ASSERT_TRUE(writer1_ptr->WaitForEvents(1, absl::Seconds(5)));
    {
        absl::MutexLock lock(writer1_ptr->mutex);
        EXPECT_EQ(writer1_ptr->events.size(), 1);
    }

    auto writer2 = std::make_unique<MockWriter>();
    auto* writer2_ptr = writer2.get();
    // writer1 is destroyed here.
    reporter.SetWriter(std::move(writer2));

    reporter.Report([](android_studio::AndroidStudioEvent& event) {
        event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
    });
    ASSERT_TRUE(writer2_ptr->WaitForEvents(1, absl::Seconds(5)));

    {
        absl::MutexLock lock(writer2_ptr->mutex);
        EXPECT_EQ(writer2_ptr->events.size(), 1);
    }
}

}  // namespace goldfish::metrics
