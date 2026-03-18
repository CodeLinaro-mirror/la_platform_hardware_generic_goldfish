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

#include "goldfish/metrics/studio_file_metrics_writer.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

#include "android/base/system.h"
#include "google_logs_publishing.pb.h"

namespace goldfish::metrics {

using namespace std::chrono_literals;

StudioFileMetricsWriter::StudioFileMetricsWriter(const fs::path& spool_dir,
                                                 const std::string& session_id,
                                                 goldfish::async::EventLoop& main_loop)
        : spool_dir_(spool_dir)
        , session_id_(session_id)
        , pid_(android::base::System::GetCurrentProcessPid())
        , max_file_duration_timer_(main_loop.ScheduleRepeating(
                  [this] {
                      absl::MutexLock lock(mutex_);
                      // Check if we should close the current file every 10s.
                      if (absl::Now() > current_file_latest_close_time_) {
                          FinalizeCurrentFile();
                      }
                  },
                  10s, 10s)) {}

StudioFileMetricsWriter::~StudioFileMetricsWriter() {
    max_file_duration_timer_->Cancel();
    absl::MutexLock lock(mutex_);
    FinalizeCurrentFile();
}

void StudioFileMetricsWriter::Write(MetricsEvent event) {
    absl::MutexLock lock(mutex_);
    if (open_file_path_.empty()) {
        OpenNextFile();
    }

    wireless_android_play_playlog::LogEvent log_event;
    log_event.set_event_time_ms(event.time_ms);
    event.as_event.SerializeToString(log_event.mutable_source_extension());

    if (!current_file_.is_open()) {
        LOG(ERROR) << "Failed to open metrics file, report will be lost: "
                   << log_event.ShortDebugString();
        return;
    }

    {
        google::protobuf::io::OstreamOutputStream raw_output(&current_file_);
        google::protobuf::io::CodedOutputStream coded_output(&raw_output);
        // Write delimited message: [size (varint)][serialized message]
        coded_output.WriteVarint32(log_event.ByteSizeLong());
        log_event.SerializeWithCachedSizes(&coded_output);
        if (coded_output.HadError()) {
            LOG(ERROR) << "Error occurred while serializing metrics report to file: "
                       << log_event.ShortDebugString() << " - " << open_file_path_;
        }
    }
    current_file_.flush();
    if (!current_file_) {
        LOG(ERROR) << "Error occurred while flushing metrics file: " << open_file_path_;
    }

    current_file_record_count_++;
    if (current_file_record_count_ >= kMaxRecordsPerFile) {
        FinalizeCurrentFile();
    }
}

void StudioFileMetricsWriter::OpenNextFile() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    CHECK(!current_file_.is_open());
    open_file_path_ = spool_dir_ / absl::StrFormat("emulator-metrics-%s-%d-%d.open", session_id_,
                                                   pid_, file_counter_++);
    current_file_.clear();
    current_file_.open(open_file_path_, std::ios::binary | std::ios::app);
    if (!current_file_) {
        LOG(ERROR) << "Error occurred while opening metrics file: " << open_file_path_;
    }
    if (!current_file_.is_open()) {
        LOG(ERROR) << "Failed to open metrics file: " << open_file_path_;
        // Keep state consistent.
        open_file_path_.clear();
    } else {
        current_file_record_count_ = 0;
        current_file_latest_close_time_ = absl::Now() + kMaxFileDuration;
    }
}

void StudioFileMetricsWriter::FinalizeCurrentFile() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (open_file_path_.empty()) {
        CHECK(!current_file_.is_open());
        return;
    }
    CHECK(current_file_.is_open());
    current_file_.clear();
    current_file_.close();
    if (!current_file_) {
        LOG(ERROR) << "Error occurred while closing metrics file: " << open_file_path_;
    }

    // Replace the .open extension with .trk, which will be detected by Studio.
    fs::path new_path = open_file_path_;
    new_path.replace_extension("trk");
    if (auto s = android::base::file::mv_file(open_file_path_, new_path); !s.ok()) {
        LOG(ERROR) << "Failed to finalize metrics file: " << s;
    }
    open_file_path_.clear();
    current_file_record_count_ = 0;
    current_file_latest_close_time_ = absl::InfiniteFuture();
}

}  // namespace goldfish::metrics
