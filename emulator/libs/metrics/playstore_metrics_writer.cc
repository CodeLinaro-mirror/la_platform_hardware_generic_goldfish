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

#include "goldfish/metrics/playstore_metrics_writer.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/base/system.h"
#include "curl/curl.h"
#include "goldfish/tools/aemu_version.h"
#include "google_logs_response.pb.h"
#include "gzip_ostream.h"

namespace goldfish::metrics {

using namespace std::chrono_literals;
using wireless_android_play_playlog::LogRequest;
using wireless_android_play_playlog::LogResponse;

namespace {

const char* get_os_type() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macosx";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

LogRequest build_base_request(const std::string& user_id) {
    LogRequest request;
    request.set_request_time_ms(android::base::System::Get()->GetUnixTimeUs() / 1000);
    request.set_log_source(LogRequest::ANDROID_STUDIO);

    auto& client = *request.mutable_client_info();
    client.set_client_type(wireless_android_play_playlog::ClientInfo::DESKTOP);

    auto& desktop = *client.mutable_desktop_client_info();
    desktop.set_application_build(VERSION);
    desktop.set_os(get_os_type());
    desktop.set_os_full_version(android::base::System::Get()->GetOsName());
    desktop.set_os_major_version(android::base::System::Get()->GetMajorOsVersion());
    desktop.set_logging_id(user_id);
    return request;
}

absl::StatusOr<std::string> serialize_and_gzip(const LogRequest& request) {
    std::ostringstream buff;
    {
        GzipOutputStream gos(buff);
        if (!request.SerializeToOstream(&gos)) {
            return absl::InternalError("Failed to serialize metrics request.");
        }
    }
    return buff.str();
}

absl::StatusOr<absl::Duration> parse_resp(std::string resp) {
    LogResponse response;
    if (!response.ParseFromString(resp)) {
        return absl::InvalidArgumentError("failed to parse server response proto");
    }
    if (!response.has_next_request_wait_millis()) {
        return absl::ZeroDuration();
    }
    auto wait = response.next_request_wait_millis();
    if (wait <= 0) {
        return absl::ZeroDuration();
    }
    return absl::Milliseconds(wait);
}

size_t curl_write_callback(char* contents, size_t size, size_t nmemb, void* userp) {
    auto& buff = *static_cast<std::string*>(userp);
    const size_t total = size * nmemb;
    buff.insert(buff.end(), contents, contents + total);
    return total;
}

struct CurlDeleter {
    void operator()(CURL* curl) const { ::curl_easy_cleanup(curl); }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* slist) const { ::curl_slist_free_all(slist); }
};

using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;

absl::Status CurlSlistAppend(CurlSlistPtr& slist, const char* string) {
    if (struct curl_slist* new_slist = ::curl_slist_append(slist.get(), string)) {
        slist.release();  // it was consumed by curl_slist_append
        slist.reset(new_slist);
        return absl::OkStatus();
    } else {
        return absl::InternalError(absl::StrCat("curl_slist_append failed for '", string, "'"));
    }
}

absl::StatusOr<absl::Duration> send_to_playstore(const std::string& url, LogRequest req) {
    VLOG(1) << "Making Clearcut POST: " << url << " - " << req.ShortDebugString();
    auto serialized = serialize_and_gzip(req);
    if (!serialized.ok()) {
        return serialized.status();
    }

    const std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
    if (!curl) {
        return absl::InternalError("failed to initialize curl");
    }

    CurlSlistPtr headers;
    if (auto s = CurlSlistAppend(headers, "Content-Encoding: gzip"); !s.ok()) {
        return s;
    }
    if (auto s = CurlSlistAppend(headers, "Content-Type: application/x-gzip"); !s.ok()) {
        return s;
    }

    std::string resp;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, serialized->data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, (long)serialized->size());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 10L);
    if (VLOG_IS_ON(1)) {
        curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, true);
    }

    CURLcode res = curl_easy_perform(curl.get());

    if (res != CURLE_OK) {
        return absl::InternalError(
                absl::StrCat("curl_easy_perform() failed: ", curl_easy_strerror(res)));
    } else {
        long http_response = 0;
        res = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_response);
        if (res != CURLE_OK) {
            return absl::InternalError(
                    absl::StrCat("curl_easy_getinfo() failed: ", curl_easy_strerror(res)));
        }

        LOG(INFO) << "Metrics written to playstore.";
        VLOG(2) << "Clearcut response: " << http_response << " - '" << resp << "'";
        if (http_response == 200) {
            return parse_resp(std::move(resp));
        } else {
            return absl::InternalError(absl::StrCat("Clearcut post failed: ", http_response));
        }
    }
}

}  // namespace

PlaystoreMetricsWriter::PlaystoreMetricsWriter(const std::string& playstore_url,
                                               const std::string& user_id,
                                               goldfish::async::EventLoop& event_loop)
        : playstore_url_(playstore_url)
        , user_id_(user_id)
        , commit_timer_(event_loop.ScheduleRepeating(
                  [this] {
                      Commit();
                      return true;
                  },
                  absl::ToChronoMilliseconds(kCommitInterval),
                  absl::ToChronoMilliseconds(kCommitInterval))) {
    curl_global_init(CURL_GLOBAL_ALL);
}

PlaystoreMetricsWriter::~PlaystoreMetricsWriter() {
    commit_timer_->Cancel();
    Commit();
    curl_global_cleanup();
}

void PlaystoreMetricsWriter::Write(MetricsEvent event) {
    wireless_android_play_playlog::LogEvent log_event;
    log_event.set_event_time_ms(event.time_ms);
    if (!event.as_event.SerializeToString(log_event.mutable_source_extension())) {
        LOG(ERROR) << "Failed to serialize metrics event.";
        return;
    }

    size_t message_length = log_event.ByteSizeLong();
    if (message_length > kMaxStorage) {
        return;
    }

    absl::MutexLock lock(&mutex_);
    while (current_bytes_ + message_length > kMaxStorage && !events_.empty()) {
        current_bytes_ -= events_.front().ByteSizeLong();
        events_.pop();
    }

    events_.push(std::move(log_event));
    current_bytes_ += message_length;
}

void PlaystoreMetricsWriter::Commit() {
    LogRequest request;
    {
        absl::MutexLock lock(&mutex_);
        if (events_.empty() ||
            absl::FromUnixMicros(android::base::System::Get()->GetUnixTimeUs()) < send_after_) {
            return;
        }

        request = build_base_request(user_id_);
        while (!events_.empty()) {
            request.add_log_event()->CopyFrom(events_.front());
            events_.pop();
        }
        current_bytes_ = 0;
    }

    if (auto wait_response = send_to_playstore(playstore_url_, std::move(request));
        !wait_response.ok()) {
        LOG(ERROR) << wait_response.status();
    } else {
        absl::MutexLock lock(&mutex_);
        if (*wait_response > absl::ZeroDuration()) {
            send_after_ = absl::FromUnixMicros(android::base::System::Get()->GetUnixTimeUs()) +
                          *wait_response;
            VLOG(1) << "Updated backoff until " << *wait_response;
        }
    }
}

}  // namespace goldfish::metrics
