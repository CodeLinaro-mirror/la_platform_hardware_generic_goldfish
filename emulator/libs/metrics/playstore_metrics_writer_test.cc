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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>

#include "absl/base/thread_annotations.h"
#include "absl/log/globals.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/base/testing/test_system.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/http/web_server.h"
#include "goldfish/tools/aemu_version.h"
#include "google_logs_publishing.pb.h"
#include "google_logs_response.pb.h"

namespace goldfish::metrics {

namespace {

using namespace std::chrono_literals;

class TestHttpServer {
  public:
    enum class Mode { kRespondOk, kRespondError, kNeverRespond };

    void Start() { Start("", Mode::kRespondOk); }

    void Start(std::string response_body) { Start(std::move(response_body), Mode::kRespondOk); }

    void Start(std::string response_body, Mode mode) {
        {
            absl::MutexLock lock(&mutex_);
            response_body_ = std::move(response_body);
            mode_ = mode;
        }
        if (mode == Mode::kNeverRespond) {
            server_.OnStream(
                    goldfish::http::HttpMethod::kPost, "/*",
                    [this](const goldfish::http::HttpRequest& req,
                           std::shared_ptr<goldfish::http::HttpResponseWriter> /*writer*/) {
                        absl::MutexLock lock(&mutex_);
                        ++request_count_;
                        last_request_body_ = std::string(req.Body());
                        // Intentionally leave stream open without responding.
                    });
        } else {
            server_.OnPost("/*", [this](const goldfish::http::HttpRequest& req) {
                absl::MutexLock lock(&mutex_);
                ++request_count_;
                last_request_body_ = std::string(req.Body());
                const auto status = (mode_ == Mode::kRespondError)
                                            ? goldfish::http::HttpStatus::kInternalServerError
                                            : goldfish::http::HttpStatus::kOk;
                return goldfish::http::HttpResponse::String(response_body_, status);
            });
        }
        ASSERT_TRUE(server_.Start(/*blocking=*/false).ok());
    }

    void SetMode(Mode mode) {
        absl::MutexLock lock(&mutex_);
        mode_ = mode;
    }

    void Stop() { server_.Stop(); }

    std::string GetUrl() const {
        return absl::StrCat("http://127.0.0.1:",
                            goldfish::network::GetPortFromEndpoint(server_.GetEndpoint()));
    }

    std::string GetHttpsUrl() const {
        return absl::StrCat("https://127.0.0.1:",
                            goldfish::network::GetPortFromEndpoint(server_.GetEndpoint()));
    }

    int GetRequestCount() {
        absl::MutexLock lock(&mutex_);
        return request_count_;
    }

    std::string GetLastRequestBody() {
        absl::MutexLock lock(&mutex_);
        return last_request_body_;
    }

  private:
    goldfish::http::WebServer server_{goldfish::http::CreateWebServer(0).BindAddress("127.0.0.1")};
    absl::Mutex mutex_;
    int request_count_ ABSL_GUARDED_BY(mutex_) = 0;
    std::string last_request_body_ ABSL_GUARDED_BY(mutex_);
    std::string response_body_ ABSL_GUARDED_BY(mutex_);
    Mode mode_ ABSL_GUARDED_BY(mutex_) = Mode::kRespondOk;
};

std::string Decompress(const std::string& compressed) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (inflateInit2(&zs, 15 + 16) != Z_OK) return "";

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    zs.avail_in = compressed.size();

    int ret;
    char outbuffer[32768];
    std::string outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = inflate(&zs, 0);
        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);
    return (ret == Z_STREAM_END) ? outstring : "";
}

MetricsEvent CreateEvent(uint64_t time_ms) {
    MetricsEvent event;
    event.time_ms = time_ms;
    event.as_event.set_kind(android_studio::AndroidStudioEvent::EMULATOR_PING);
    return event;
}

}  // namespace

class PlaystoreMetricsWriterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_system_ = std::make_unique<android::base::TestSystem>("/tmp", "/home", "/appdata");
        test_system_->SetUnixTime(1000000);
        event_loop_ = goldfish::async::testing::TestEventLoop::Create();
    }

    void VerifyRequest(const std::string& body, const std::string& expected_user,
                       int expected_events) {
        ASSERT_FALSE(body.empty());
        std::string decompressed = Decompress(body);
        ASSERT_FALSE(decompressed.empty());

        wireless_android_play_playlog::LogRequest request;
        ASSERT_TRUE(request.ParseFromString(decompressed));

        EXPECT_EQ(request.log_source(), wireless_android_play_playlog::LogRequest::ANDROID_STUDIO);
        ASSERT_TRUE(request.has_client_info());
        auto& desktop = request.client_info().desktop_client_info();
        EXPECT_EQ(desktop.logging_id(), expected_user);
        EXPECT_EQ(desktop.application_build(), goldfish::version::GetEmulatorVersion());
        EXPECT_EQ(request.log_event_size(), expected_events);
    }

    std::unique_ptr<android::base::TestSystem> test_system_;
    std::unique_ptr<goldfish::async::testing::TestEventLoop> event_loop_;
};

TEST_F(PlaystoreMetricsWriterTest, SendDataToServer) {
    TestHttpServer server;
    server.Start();
    {
        PlaystoreMetricsWriter writer(server.GetUrl(), "test-user", *event_loop_);
        writer.Write(CreateEvent(12345));

        // No request yet
        EXPECT_EQ(server.GetRequestCount(), 0);

        // Advance clock by 10 minutes to trigger periodic commit
        event_loop_->AdvanceClock(absl::Minutes(10));
    }
    server.Stop();
    EXPECT_GE(server.GetRequestCount(), 1);
    VerifyRequest(server.GetLastRequestBody(), "test-user", 1);
}

TEST_F(PlaystoreMetricsWriterTest, RespectsBackoff) {
    TestHttpServer server;
    wireless_android_play_playlog::LogResponse log_response;
    log_response.set_next_request_wait_millis(5000);
    server.Start(log_response.SerializeAsString());

    PlaystoreMetricsWriter writer(server.GetUrl(), "test-user", *event_loop_);

    // First write and commit
    writer.Write(CreateEvent(test_system_->GetUnixTimeUs() / 1000));
    event_loop_->AdvanceClock(absl::Minutes(10));
    EXPECT_EQ(server.GetRequestCount(), 1);

    // Second write and commit immediately after should be skipped (in-memory backoff)
    writer.Write(CreateEvent(test_system_->GetUnixTimeUs() / 1000));
    event_loop_->AdvanceClock(absl::Minutes(10));
    EXPECT_EQ(server.GetRequestCount(), 1);

    // Advance system time to bypass backoff
    test_system_->SetUnixTime(test_system_->GetUnixTime() + 6);

    // Third commit should now proceed
    event_loop_->AdvanceClock(absl::Minutes(10));
    EXPECT_EQ(server.GetRequestCount(), 2);
    server.Stop();
}

TEST_F(PlaystoreMetricsWriterTest, StorageLimitDropsOldEvents) {
    TestHttpServer server;
    server.Start();
    {
        PlaystoreMetricsWriter writer(server.GetUrl(), "test-user", *event_loop_);
        for (int i = 0; i < 2000; ++i) {
            writer.Write(CreateEvent(i));
        }
        event_loop_->AdvanceClock(absl::Minutes(10));
    }
    EXPECT_EQ(server.GetRequestCount(), 1);
    server.Stop();
}

TEST_F(PlaystoreMetricsWriterTest, MetadataIsCorrect) {
    TestHttpServer server;
    server.Start();
    std::string test_user = "metadata-test-user";
    {
        PlaystoreMetricsWriter writer(server.GetUrl(), test_user, *event_loop_);
        writer.Write(CreateEvent(100));
        event_loop_->AdvanceClock(absl::Minutes(10));
    }
    server.Stop();

    EXPECT_EQ(server.GetRequestCount(), 1);
    VerifyRequest(server.GetLastRequestBody(), test_user, 1);
}

TEST_F(PlaystoreMetricsWriterTest, HttpsImportsNativeCaStore) {
    int old_vlog = absl::SetGlobalVLogLevel(1);
    TestHttpServer server;
    server.Start();
    testing::internal::CaptureStderr();
    {
        PlaystoreMetricsWriter writer(server.GetHttpsUrl(), "test-user", *event_loop_);
        writer.Write(CreateEvent(12345));
        event_loop_->AdvanceClock(absl::Minutes(10));
    }
    server.Stop();
    std::string stderr_output = testing::internal::GetCapturedStderr();
    absl::SetGlobalVLogLevel(old_vlog);

    EXPECT_THAT(stderr_output, ::testing::HasSubstr("ALPN: curl offers http/1.1"));
    EXPECT_THAT(stderr_output, ::testing::HasSubstr("TLS connect error"));
#ifdef _WIN32
    EXPECT_THAT(stderr_output, ::testing::HasSubstr("successfully imported Windows ROOT store"));
    EXPECT_THAT(stderr_output, ::testing::HasSubstr("successfully imported Windows CA store"));
#endif
}

TEST_F(PlaystoreMetricsWriterTest, RequeuesFailedUploadWhileRunning) {
    TestHttpServer server;
    server.Start("", TestHttpServer::Mode::kRespondError);
    {
        PlaystoreMetricsWriter writer(server.GetUrl(), "test-user", *event_loop_);
        writer.Write(CreateEvent(12345));

        // First commit fails with HTTP 500; the event should be re-queued.
        event_loop_->AdvanceClock(absl::Minutes(10));
        EXPECT_EQ(server.GetRequestCount(), 1);

        // Next commit succeeds and delivers the re-queued event.
        server.SetMode(TestHttpServer::Mode::kRespondOk);
        event_loop_->AdvanceClock(absl::Minutes(10));
        EXPECT_EQ(server.GetRequestCount(), 2);
    }
    server.Stop();
    VerifyRequest(server.GetLastRequestBody(), "test-user", 1);
}

TEST_F(PlaystoreMetricsWriterTest, DestructorCancelsStalledUploadWithinShutdownBudget) {
    TestHttpServer server;
    server.Start("", TestHttpServer::Mode::kNeverRespond);
    const absl::Time start = absl::Now();
    {
        PlaystoreMetricsWriter writer(server.GetUrl(), "test-user", *event_loop_);
        writer.Write(CreateEvent(12345));
    }
    const absl::Duration elapsed = absl::Now() - start;
    server.Stop();

    EXPECT_EQ(server.GetRequestCount(), 1);
    // Destructor enforces the 2s shutdown budget rather than waiting for the 15s upload timeout.
    EXPECT_LT(elapsed, absl::Seconds(6));
}

}  // namespace goldfish::metrics
