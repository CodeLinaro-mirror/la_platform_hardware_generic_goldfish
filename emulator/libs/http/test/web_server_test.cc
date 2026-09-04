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
#include "goldfish/http/web_server.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"

namespace goldfish::http {

using namespace std::chrono_literals;

class WebServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        event_loop_ = async::LibuvEventLoop::Create("HttpTestLoop");
        raw_loop_ = event_loop_.get();
        socket_factory_ = std::make_shared<async::LibuvAsyncSocketFactory>();

        absl::Notification loop_running;
        auto sub = raw_loop_->AddCallback([&](const async::LooperStatusEvent& ev) {
            if (ev.state == async::LooperStatusEvent::State::kRunning) {
                loop_running.Notify();
            }
        });

        loop_thread_ = std::thread([this] { raw_loop_->Run().IgnoreError(); });
        loop_running.WaitForNotification();
    }

    void TearDown() override {
        auto status = raw_loop_->ShutdownAndWait(absl::Seconds(1));
        EXPECT_TRUE(status.ok());
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    std::string SendHttpRequest(const network::Endpoint& endpoint, std::string request_payload) {
        auto promise_ptr = std::make_shared<std::promise<std::string>>();
        auto response_future = promise_ptr->get_future();

        raw_loop_
                ->Post([this, endpoint, request_payload = std::move(request_payload),
                        promise_ptr]() {
                    auto client_sock = socket_factory_->CreateSocket(raw_loop_, endpoint);
                    auto client_holder =
                            std::make_shared<std::shared_ptr<async::AsyncSocket>>(client_sock);
                    auto response_buf = std::make_shared<std::string>();
                    auto done = std::make_shared<std::atomic<bool>>(false);

                    auto fulfill_once = [response_buf, promise_ptr, done,
                                         weak_sock =
                                                 std::weak_ptr<async::AsyncSocket>(client_sock)]() {
                        if (!done->exchange(true)) {
                            promise_ptr->set_value(*response_buf);
                            if (auto sock = weak_sock.lock()) {
                                sock->Close();
                            }
                        }
                    };

                    client_sock->SetOnConnectedCallback(
                            [weak_sock = std::weak_ptr<async::AsyncSocket>(client_sock),
                             payload = std::move(request_payload)](async::AsyncSocket&,
                                                                   absl::Status status) {
                                if (status.ok()) {
                                    if (auto sock = weak_sock.lock()) {
                                        sock->Send(payload.data(), payload.size()).IgnoreError();
                                    }
                                }
                            });

                    client_sock->SetOnReadCallbackNoFlowControl(
                            [response_buf, fulfill_once](std::string_view data,
                                                         absl::Status status) {
                                if (status.ok()) {
                                    response_buf->append(data);
                                    // If we received end of message or close, fulfill promise
                                    if (absl::StrContains(*response_buf, "\r\n\r\n")) {
                                        if (absl::StrContains(absl::AsciiStrToLower(*response_buf),
                                                              headers::kContentLength) ||
                                            absl::StrContains(*response_buf, "\r\n0\r\n\r\n") ||
                                            absl::EndsWith(*response_buf, "0\r\n\r\n") ||
                                            absl::StartsWith(*response_buf, "HTTP/1.1 204") ||
                                            absl::StartsWith(*response_buf, "HTTP/1.1 304") ||
                                            absl::StartsWith(*response_buf, "HTTP/1.1 1")) {
                                            fulfill_once();
                                        }
                                    }
                                } else {
                                    fulfill_once();
                                }
                            });

                    client_sock->SetOnCloseCallback([client_holder, fulfill_once]() {
                        fulfill_once();
                        client_holder->reset();
                    });

                    client_sock->Connect().IgnoreError();
                })
                .IgnoreError();

        auto status = response_future.wait_for(2000ms);
        EXPECT_EQ(status, std::future_status::ready);
        if (status == std::future_status::ready) {
            return response_future.get();
        }
        return {};
    }

    std::unique_ptr<async::LibuvEventLoop> event_loop_;
    async::LibuvEventLoop* raw_loop_;
    std::shared_ptr<async::AsyncSocketFactory> socket_factory_;
    std::thread loop_thread_;
};

TEST_F(WebServerTest, EndToEndUnaryGetAndPost) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    ws.OnGet("/hello", [](const HttpRequest& req) {
        EXPECT_EQ(req.Method(), HttpMethod::kGet);
        return HttpResponse::String("Hello, World!", 200, "text/plain");
    });

    ws.OnPost("/echo", [](const HttpRequest& req) {
        EXPECT_EQ(req.Method(), HttpMethod::kPost);
        return HttpResponse::String(std::string(req.Body()), 200, "text/plain");
    });

    auto start_status = ws.Start();
    ASSERT_TRUE(start_status.ok());

    // Duplicate start returns error
    EXPECT_FALSE(ws.Start().ok());

    auto endpoint = ws.GetEndpoint();

    // 1. Test GET /hello
    std::string get_resp =
            SendHttpRequest(endpoint, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(get_resp, "HTTP/1.1 200 OK\r\n"));
    EXPECT_TRUE(absl::EndsWith(get_resp, "\r\n\r\nHello, World!"));

    // 2. Test POST /echo
    std::string post_resp = SendHttpRequest(
            endpoint,
            "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 11\r\n\r\nEchoPayload");
    EXPECT_TRUE(absl::StartsWith(post_resp, "HTTP/1.1 200 OK\r\n"));
    EXPECT_TRUE(absl::EndsWith(post_resp, "\r\n\r\nEchoPayload"));

    ws.Stop();
}

TEST_F(WebServerTest, ImproperConfigurationReturnsError) {
    // 1. Invalid bind address format
    auto bad_ip_cfg = CreateWebServer(8080).BindAddress("invalid.ip.address.format");
    EXPECT_FALSE(bad_ip_cfg.Validate().ok());
    EXPECT_EQ(bad_ip_cfg.Validate().code(), absl::StatusCode::kInvalidArgument);

    WebServer bad_ip_ws{bad_ip_cfg};
    auto start_bad_ip = bad_ip_ws.Start();
    EXPECT_FALSE(start_bad_ip.ok());
    EXPECT_EQ(start_bad_ip.code(), absl::StatusCode::kInvalidArgument);

    // 2. Zero max payload size
    auto zero_payload_cfg = CreateWebServer(8080).MaxPayloadSize(0);
    EXPECT_FALSE(zero_payload_cfg.Validate().ok());
    EXPECT_EQ(zero_payload_cfg.Validate().code(), absl::StatusCode::kInvalidArgument);

    WebServer zero_payload_ws{zero_payload_cfg};
    auto start_zero = zero_payload_ws.Start();
    EXPECT_FALSE(start_zero.ok());
    EXPECT_EQ(start_zero.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(WebServerTest, EndToEndCorsPreflightOptions) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    ws.OnOptions("/*", [](const HttpRequest& req) {
        return HttpResponse::Empty(204)
                .WithHeader("Access-Control-Allow-Origin", "*")
                .WithHeader("Access-Control-Allow-Methods", "POST, OPTIONS, GET")
                .WithHeader("Access-Control-Allow-Headers", "content-type,x-grpc-web");
    });

    auto start_status = ws.Start();
    ASSERT_TRUE(start_status.ok());

    auto endpoint = ws.GetEndpoint();

    std::string options_resp =
            SendHttpRequest(endpoint,
                            "OPTIONS /api/service HTTP/1.1\r\nHost: localhost\r\nOrigin: "
                            "http://localhost:3000\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(options_resp, "HTTP/1.1 204 No Content\r\n"));
    EXPECT_TRUE(absl::StrContains(options_resp, "Access-Control-Allow-Origin: *\r\n"));
    EXPECT_TRUE(absl::StrContains(options_resp,
                                  "Access-Control-Allow-Methods: POST, OPTIONS, GET\r\n"));

    ws.Stop();
}

TEST_F(WebServerTest, EndToEndStreamingHandler) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    ws.OnStream(HttpMethod::kPost, "/stream",
                [](const HttpRequest& req, std::shared_ptr<HttpResponseWriter> writer) {
                    writer->SendHeaders({{"Content-Type", "application/octet-stream"}});
                    writer->SendChunk("PART_1;");
                    writer->SendChunk("PART_2;");
                    writer->SendChunk("CHUNK_END");
                    writer->Finish();
                });

    auto start_status = ws.Start();
    ASSERT_TRUE(start_status.ok());

    auto endpoint = ws.GetEndpoint();

    std::string stream_resp =
            SendHttpRequest(endpoint, "POST /stream HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(stream_resp, "HTTP/1.1 200 OK\r\n"));
    EXPECT_TRUE(absl::StrContains(stream_resp, "Content-Type: application/octet-stream\r\n"));
    EXPECT_TRUE(absl::StrContains(stream_resp, "PART_1;PART_2;CHUNK_END"));

    ws.Stop();
}

TEST_F(WebServerTest, ConcurrentClientRequests) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    std::atomic<int> req_count{0};
    ws.OnGet("/ping", [&req_count](const HttpRequest&) {
        req_count.fetch_add(1, std::memory_order_relaxed);
        return HttpResponse::String("pong");
    });

    ASSERT_TRUE(ws.Start().ok());
    auto endpoint = ws.GetEndpoint();

    constexpr int kNumClients = 8;
    std::vector<std::future<std::string>> futures;
    futures.reserve(kNumClients);

    for (int i = 0; i < kNumClients; ++i) {
        futures.push_back(std::async(std::launch::async, [&]() {
            return SendHttpRequest(endpoint, "GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n");
        }));
    }

    for (auto& f : futures) {
        std::string resp = f.get();
        EXPECT_TRUE(absl::StartsWith(resp, "HTTP/1.1 200 OK\r\n"));
        EXPECT_TRUE(absl::EndsWith(resp, "\r\n\r\npong"));
    }

    EXPECT_EQ(req_count.load(), kNumClients);
    ws.Stop();
}

TEST_F(WebServerTest, AccessLogCallbackInvokedWithTelemetry) {
    absl::Mutex log_mutex;
    std::vector<AccessLogEntry> logged_entries;

    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)
                         .AccessLog([&](const AccessLogEntry& entry) {
                             const absl::MutexLock lock(&log_mutex);
                             logged_entries.push_back(entry);
                         })};

    ws.OnGet("/api/data", [](const HttpRequest&) {
        return HttpResponse::String("PayloadData", HttpStatus::kOk);
    });

    ws.OnStream(HttpMethod::kPost, "/api/stream",
                [](const HttpRequest&, std::shared_ptr<HttpResponseWriter> writer) {
                    writer->SendHeaders({{"content-type", "text/plain"}}, HttpStatus::kOk);
                    writer->SendChunk("StreamChunk1");
                    writer->SendChunk("StreamChunk2");
                    writer->Finish();
                });

    ASSERT_TRUE(ws.Start().ok());
    auto endpoint = ws.GetEndpoint();

    // 1. Unary request
    std::string unary_resp =
            SendHttpRequest(endpoint, "GET /api/data?query=1 HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(unary_resp, "HTTP/1.1 200 OK\r\n"));

    // 2. Streaming request
    std::string stream_resp =
            SendHttpRequest(endpoint, "POST /api/stream HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(stream_resp, "HTTP/1.1 200 OK\r\n"));

    // 3. 404 Request
    std::string not_found_resp =
            SendHttpRequest(endpoint, "GET /unknown/path HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(not_found_resp, "HTTP/1.1 404 Not Found\r\n"));

    ws.Stop();

    // Verify access logs
    const absl::MutexLock lock(&log_mutex);
    ASSERT_EQ(logged_entries.size(), 3);

    // Unary entry check
    EXPECT_EQ(logged_entries[0].method, HttpMethod::kGet);
    EXPECT_EQ(logged_entries[0].path, "/api/data?query=1");
    EXPECT_EQ(logged_entries[0].status_code, 200);
    EXPECT_EQ(logged_entries[0].bytes_sent, std::string("PayloadData").size());
    EXPECT_GT(logged_entries[0].timestamp, absl::UnixEpoch());
    EXPECT_GE(logged_entries[0].duration, absl::ZeroDuration());

    // Stream entry check
    EXPECT_EQ(logged_entries[1].method, HttpMethod::kPost);
    EXPECT_EQ(logged_entries[1].path, "/api/stream");
    EXPECT_EQ(logged_entries[1].status_code, 200);
    EXPECT_EQ(logged_entries[1].bytes_sent,
              std::string("StreamChunk1").size() + std::string("StreamChunk2").size());
    EXPECT_GE(logged_entries[1].duration, absl::ZeroDuration());

    // 404 entry check
    EXPECT_EQ(logged_entries[2].method, HttpMethod::kGet);
    EXPECT_EQ(logged_entries[2].path, "/unknown/path");
    EXPECT_EQ(logged_entries[2].status_code, 404);
}

TEST_F(WebServerTest, ClientDisconnectTriggersWriterOnClose) {
    auto disconnect_done = std::make_shared<std::atomic<bool>>(false);
    auto disconnect_promise = std::make_shared<std::promise<void>>();
    auto disconnect_future = disconnect_promise->get_future();

    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    ws.OnStream(HttpMethod::kGet, "/long-stream",
                [disconnect_done, disconnect_promise](const HttpRequest&,
                                                      std::shared_ptr<HttpResponseWriter> writer) {
                    writer->SendHeaders({{"content-type", "text/plain"}});
                    writer->SendChunk("InitialChunk\n");
                    writer->SetOnCloseCallback([disconnect_done, disconnect_promise]() {
                        if (!disconnect_done->exchange(true)) {
                            disconnect_promise->set_value();
                        }
                    });
                });

    ASSERT_TRUE(ws.Start().ok());
    auto endpoint = ws.GetEndpoint();

    // Client connects, receives initial chunk, then closes the socket abruptly
    raw_loop_
            ->Post([&]() {
                auto client_sock = socket_factory_->CreateSocket(raw_loop_, endpoint);
                auto client_holder =
                        std::make_shared<std::shared_ptr<async::AsyncSocket>>(client_sock);
                client_sock->SetOnConnectedCallback(
                        [weak_sock = std::weak_ptr<async::AsyncSocket>(client_sock)](
                                async::AsyncSocket&, absl::Status status) {
                            if (status.ok()) {
                                if (auto sock = weak_sock.lock()) {
                                    std::string req =
                                            "GET /long-stream HTTP/1.1\r\nHost: localhost\r\n\r\n";
                                    sock->Send(req.data(), req.size()).IgnoreError();
                                }
                            }
                        });

                client_sock->SetOnReadCallbackNoFlowControl(
                        [weak_sock = std::weak_ptr<async::AsyncSocket>(client_sock)](
                                std::string_view data, absl::Status status) {
                            if (status.ok() && absl::StrContains(data, "InitialChunk")) {
                                if (auto sock = weak_sock.lock()) {
                                    sock->Close();
                                }
                            }
                        });

                client_sock->SetOnCloseCallback([client_holder]() { client_holder->reset(); });

                client_sock->Connect().IgnoreError();
            })
            .IgnoreError();

    auto status = disconnect_future.wait_for(2000ms);
    EXPECT_EQ(status, std::future_status::ready);

    ws.Stop();
}

TEST_F(WebServerTest, ParallelKeepAliveStressTest) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    std::atomic<int> request_counter{0};
    ws.OnGet("/fast", [&request_counter](const HttpRequest&) {
        request_counter.fetch_add(1, std::memory_order_relaxed);
        return HttpResponse::String("OK");
    });

    ASSERT_TRUE(ws.Start().ok());
    auto endpoint = ws.GetEndpoint();

    constexpr int kClients = 8;
    constexpr int kReqsPerClient = 10;
    std::vector<std::future<void>> client_futures;
    client_futures.reserve(kClients);

    for (int c = 0; c < kClients; ++c) {
        client_futures.push_back(std::async(std::launch::async, [&]() {
            for (int r = 0; r < kReqsPerClient; ++r) {
                std::string resp =
                        SendHttpRequest(endpoint, "GET /fast HTTP/1.1\r\nHost: localhost\r\n\r\n");
                EXPECT_TRUE(absl::StartsWith(resp, "HTTP/1.1 200 OK\r\n"));
            }
        }));
    }

    for (auto& f : client_futures) {
        f.get();
    }

    EXPECT_EQ(request_counter.load(), kClients * kReqsPerClient);
    ws.Stop();
}

TEST_F(WebServerTest, MultipleStopCallsAreSafeAndIdempotent) {
    WebServer ws{CreateWebServer(0)
                         .BindAddress("127.0.0.1")
                         .EventLoop(raw_loop_)
                         .SocketFactory(socket_factory_)};

    ws.OnGet("/test", [](const HttpRequest&) { return HttpResponse::String("OK"); });

    ASSERT_TRUE(ws.Start().ok());
    auto endpoint = ws.GetEndpoint();

    std::string resp = SendHttpRequest(endpoint, "GET /test HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_TRUE(absl::StartsWith(resp, "HTTP/1.1 200 OK\r\n"));

    // Multiple sequential calls to Stop() must be completely safe, leak-free, and not abort
    ws.Stop();
    ws.Stop();
    ws.Stop();
}

}  // namespace goldfish::http
