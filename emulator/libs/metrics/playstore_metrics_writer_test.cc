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

#include <gtest/gtest.h>
#include <sys/types.h>
#include <zlib.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "absl/strings/str_format.h"

#include "android/base/testing/test_system.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/tools/aemu_version.h"
#include "google_logs_publishing.pb.h"
#include "google_logs_response.pb.h"

namespace goldfish::metrics {

namespace {

using namespace std::chrono_literals;

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

class TestHttpServer {
  public:
    TestHttpServer() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        server_addr.sin_port = 0;

        bind(server_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
        listen(server_fd_, 1);

        socklen_t addr_len = sizeof(server_addr);
        getsockname(server_fd_, (struct sockaddr*)&server_addr, &addr_len);
        port_ = ntohs(server_addr.sin_port);
    }

    ~TestHttpServer() {
        Stop();
#ifdef _WIN32
        closesocket(server_fd_);
        WSACleanup();
#else
        close(server_fd_);
#endif
    }

    std::string GetUrl() const { return absl::StrFormat("http://127.0.0.1:%d", port_); }

    void Start(const std::string& response_body = "") {
        stop_ = false;
        request_count_ = 0;
        last_request_body_.clear();
        server_thread_ = std::thread([this, response_body]() {
            while (!stop_) {
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(server_fd_, &read_fds);
                timeval tv{0, 100000};
                if (select(server_fd_ + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
                    int client_fd = accept(server_fd_, nullptr, nullptr);
                    if (client_fd >= 0) {
                        char buffer[16384];
                        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), 0);
                        if (bytes > 0) {
                            std::string full_request(buffer, bytes);
                            size_t body_pos = full_request.find("\r\n\r\n");
                            if (body_pos != std::string::npos) {
                                last_request_body_ = full_request.substr(body_pos + 4);
                            }
                        }
                        request_count_++;

                        std::string response =
                                absl::StrFormat("HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s",
                                                response_body.size(), response_body);
                        send(client_fd, response.data(), response.size(), 0);
#ifdef _WIN32
                        closesocket(client_fd);
#else
                        close(client_fd);
#endif
                    }
                }
            }
        });
    }

    void Stop() {
        stop_ = true;
        if (server_thread_.joinable()) server_thread_.join();
    }

    int GetRequestCount() const { return request_count_; }
    std::string GetLastRequestBody() const { return last_request_body_; }

  private:
    int server_fd_;
    int port_;
    std::thread server_thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> request_count_{0};
    std::string last_request_body_;
};

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

}  // namespace goldfish::metrics
