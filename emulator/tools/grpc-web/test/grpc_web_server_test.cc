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
#include "goldfish/grpcweb/grpc_web_server.h"

#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include <charconv>
#include <chrono>
#include <future>
#include <thread>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/grpcweb/grpc_web_protocol.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::grpcweb {

using namespace std::chrono_literals;

class GrpcWebServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        event_loop_ = async::LibuvEventLoop::Create();
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

        // Start mock gRPC server
        cq_ = grpc_builder_.AddCompletionQueue();
        grpc_builder_.RegisterAsyncGenericService(&generic_service_);
        grpc_server_ = grpc_builder_.BuildAndStart();
        ASSERT_NE(grpc_server_, nullptr);
        grpc_channel_ = grpc_server_->InProcessChannel(grpc::ChannelArguments());
    }

    void TearDown() override {
        grpc_channel_.reset();
        grpc_server_->Shutdown();
        cq_->Shutdown();
        void* tag;
        bool ok;
        while (cq_->Next(&tag, &ok)) {
        }
        grpc_server_->Wait();

        auto s = raw_loop_->ShutdownAndWait(absl::Seconds(1));
        EXPECT_TRUE(s.ok());
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    bool WaitForTag(void* expected_tag) {
        void* tag = nullptr;
        bool ok = false;
        while (cq_->Next(&tag, &ok)) {
            if (tag == expected_tag) {
                return ok;
            }
        }
        return false;
    }

    template <typename F>
    auto PostAndWait(F&& func) -> decltype(func()) {
        auto res = raw_loop_->PostAndWait(std::forward<F>(func));
        EXPECT_TRUE(res.ok());
        if constexpr (std::is_void_v<decltype(func())>) {
            return;
        } else {
            return std::move(*res);
        }
    }

    std::unique_ptr<async::LibuvEventLoop> event_loop_;
    async::LibuvEventLoop* raw_loop_;
    std::shared_ptr<async::AsyncSocketFactory> socket_factory_;
    std::thread loop_thread_;

    grpc::AsyncGenericService generic_service_;
    grpc::ServerBuilder grpc_builder_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    std::unique_ptr<grpc::Server> grpc_server_;
    std::shared_ptr<grpc::Channel> grpc_channel_;
};

TEST_F(GrpcWebServerTest, EndToEndCorsPreflight) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                std::string req =
                        "OPTIONS /emulator.EmulatorService/GetStatus HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "Origin: http://localhost:3000\r\n"
                        "Access-Control-Request-Method: POST\r\n"
                        "Access-Control-Request-Headers: content-type,x-grpc-web\r\n"
                        "\r\n";
                s.Send(req.data(), req.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 204 No Content"), std::string::npos);
    EXPECT_NE(received_data.find("access-control-allow-origin: http://localhost:3000"),
              std::string::npos);
}

TEST_F(GrpcWebServerTest, RejectsCorsOriginNotInAllowlist) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_,
                                     "http://trusted.com");
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                std::string req =
                        "OPTIONS /emulator.EmulatorService/GetStatus HTTP/1.1\r\n"
                        "Host: 127.0.0.1\r\n"
                        "Origin: http://evil.com\r\n"
                        "Access-Control-Request-Method: POST\r\n"
                        "\r\n";
                s.Send(req.data(), req.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 403 Forbidden"), std::string::npos);
}

TEST_F(GrpcWebServerTest, RejectsCorsPostWithUnauthorizedOrigin) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_,
                                     "http://trusted.com");
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    std::string payload = Protocol::PackFrame(kFrameData, "test");
    std::string http_post =
            "POST /emulator.EmulatorService/GetStatus HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: http://evil.com\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(payload.size()) + "\r\n\r\n" + payload;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 403 Forbidden"), std::string::npos);
}

TEST_F(GrpcWebServerTest, RejectsMalformedGrpcMethodPath) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    std::string payload = Protocol::PackFrame(kFrameData, "test");
    std::string http_post =
            "POST /invalid_path HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(payload.size()) + "\r\n\r\n" + payload;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

TEST_F(GrpcWebServerTest, RejectsInvalidContentType) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    std::string http_post =
            "POST /emulator.EmulatorService/GetStatus HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 5\r\n\r\nhello";

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 415 Unsupported Media Type"), std::string::npos);
}

TEST_F(GrpcWebServerTest, RejectsUnsupportedMethod) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification response_received;
    std::string received_data;

    std::string http_get =
            "GET /emulator.EmulatorService/GetStatus HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n\r\n";

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("\r\n\r\n") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_get](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_get.data(), http_get.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });

    EXPECT_NE(received_data.find("HTTP/1.1 405 Method Not Allowed"), std::string::npos);
}

bool IsChunkedResponseComplete(std::string_view resp) {
    size_t header_end = resp.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        return false;
    }
    std::string_view body = resp.substr(header_end + 4);
    return absl::EndsWith(body, "\r\n0\r\n\r\n") || body == "0\r\n\r\n";
}

TEST_F(GrpcWebServerTest, EndToEndGrpcEchoCall) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    // Handle gRPC request in background
    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (WaitForTag(reinterpret_cast<void*>(1))) {
            auto client_metadata = server_ctx.client_metadata();
            auto auth_it = client_metadata.find("authorization");
            EXPECT_NE(auth_it, client_metadata.end());
            if (auth_it != client_metadata.end()) {
                EXPECT_EQ(std::string(auth_it->second.data(), auth_it->second.size()),
                          "Bearer test-jwt-token-123");
            }
            auto custom_it = client_metadata.find("x-custom-auth");
            EXPECT_NE(custom_it, client_metadata.end());
            if (custom_it != client_metadata.end()) {
                EXPECT_EQ(std::string(custom_it->second.data(), custom_it->second.size()),
                          "custom-token-456");
            }

            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (WaitForTag(reinterpret_cast<void*>(2))) {
                stream.Write(recv_bb, reinterpret_cast<void*>(3));
                WaitForTag(reinterpret_cast<void*>(3));
            }
            stream.Finish(grpc::Status::OK, reinterpret_cast<void*>(4));
            WaitForTag(reinterpret_cast<void*>(4));
        }
    });

    std::string echo_payload = "Hello from gRPC-Web Test!";
    std::string data_frame = Protocol::PackFrame(kFrameData, echo_payload);
    std::string http_post =
            "POST /test.EchoService/Echo HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Authorization: Bearer test-jwt-token-123\r\n"
            "X-Custom-Auth: custom-token-456\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(data_frame.size()) +
            "\r\n"
            "\r\n" +
            data_frame;

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (IsChunkedResponseComplete(received_data)) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });
    responder.join();

    EXPECT_NE(received_data.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(received_data.find("content-type: application/grpc-web+proto"), std::string::npos);
}

TEST_F(GrpcWebServerTest, TextModeBase64EchoCall) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (WaitForTag(reinterpret_cast<void*>(1))) {
            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (WaitForTag(reinterpret_cast<void*>(2))) {
                stream.Write(recv_bb, reinterpret_cast<void*>(3));
                WaitForTag(reinterpret_cast<void*>(3));
            }
            stream.Finish(grpc::Status::OK, reinterpret_cast<void*>(4));
            WaitForTag(reinterpret_cast<void*>(4));
        }
    });

    std::string echo_payload = "text-payload-e2e";
    std::string data_frame = Protocol::PackFrame(kFrameData, echo_payload);
    std::string base64_frame = absl::Base64Escape(data_frame);

    std::string http_post =
            "POST /test.EchoService/Echo HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/grpc-web-text\r\n"
            "Accept: application/grpc-web-text\r\n"
            "Content-Length: " +
            std::to_string(base64_frame.size()) + "\r\n\r\n" + base64_frame;

    std::string trailer_frame = Protocol::PackFrame(kFrameTrailer, "grpc-status:0\r\n");
    std::string expected_trailer_b64 = absl::Base64Escape(trailer_frame);

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (IsChunkedResponseComplete(received_data)) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });
    responder.join();

    EXPECT_NE(received_data.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(received_data.find("content-type: application/grpc-web-text+proto"),
              std::string::npos);

    // Verify both base64-encoded data frame and trailer are present in chunked response
    EXPECT_NE(received_data.find(base64_frame), std::string::npos);
    EXPECT_NE(received_data.find(expected_trailer_b64), std::string::npos);
}

TEST_F(GrpcWebServerTest, TextModeMultiChunkOddByteStreaming) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (WaitForTag(reinterpret_cast<void*>(1))) {
            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (WaitForTag(reinterpret_cast<void*>(2))) {
                // Stream 3 responses with odd byte lengths to stress streaming Base64 encoding
                // Frame 1: 5-byte header + 8-byte payload = 13 bytes (remainder 1)
                std::string msg1 = "Payload1";
                auto bb1 = Protocol::StringToByteBuffer(msg1);
                stream.Write(bb1, reinterpret_cast<void*>(3));
                WaitForTag(reinterpret_cast<void*>(3));

                // Frame 2: 5-byte header + 7-byte payload = 12 bytes (remainder 0)
                std::string msg2 = "Payload";
                auto bb2 = Protocol::StringToByteBuffer(msg2);
                stream.Write(bb2, reinterpret_cast<void*>(4));
                WaitForTag(reinterpret_cast<void*>(4));

                // Frame 3: 5-byte header + 11-byte payload = 16 bytes (remainder 1)
                std::string msg3 = "PayloadThree";
                auto bb3 = Protocol::StringToByteBuffer(msg3);
                stream.Write(bb3, reinterpret_cast<void*>(5));
                WaitForTag(reinterpret_cast<void*>(5));
            }
            stream.Finish(grpc::Status::OK, reinterpret_cast<void*>(6));
            WaitForTag(reinterpret_cast<void*>(6));
        }
    });

    std::string echo_payload = "start-stream";
    std::string data_frame = Protocol::PackFrame(kFrameData, echo_payload);
    std::string base64_frame = absl::Base64Escape(data_frame);

    std::string http_post =
            "POST /test.EchoService/Echo HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/grpc-web-text\r\n"
            "Accept: application/grpc-web-text\r\n"
            "Content-Length: " +
            std::to_string(base64_frame.size()) + "\r\n\r\n" + base64_frame;

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (IsChunkedResponseComplete(received_data)) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });
    responder.join();

    EXPECT_NE(received_data.find("HTTP/1.1 200 OK"), std::string::npos);

    // Extract all chunk bodies from the HTTP response
    size_t header_end = received_data.find("\r\n\r\n");
    ASSERT_NE(header_end, std::string::npos);
    std::string body_section = received_data.substr(header_end + 4);

    // Filter out HTTP chunked framing lengths and CRLFs to get the continuous Base64 text
    std::string raw_b64;
    std::string_view remaining = body_section;
    while (!remaining.empty()) {
        while (remaining.starts_with("\r\n")) {
            remaining.remove_prefix(2);
        }
        if (remaining.empty()) {
            break;
        }
        size_t crlf = remaining.find("\r\n");
        if (crlf == std::string_view::npos) {
            break;
        }
        std::string_view size_str = remaining.substr(0, crlf);
        uint32_t chunk_len = 0;
        auto [ptr, ec] =
                std::from_chars(size_str.data(), size_str.data() + size_str.size(), chunk_len, 16);
        if (ec != std::errc() || chunk_len == 0) {
            break;
        }
        remaining.remove_prefix(crlf + 2);
        if (remaining.size() < chunk_len) {
            break;
        }
        raw_b64.append(remaining.substr(0, chunk_len));
        remaining.remove_prefix(chunk_len);
    }

    // Decode the concatenated Base64 stream
    std::string decoded_binary;
    ASSERT_TRUE(absl::Base64Unescape(raw_b64, &decoded_binary))
            << "Failed to unescape raw_b64: '" << raw_b64 << "', body_section: '" << body_section
            << "'";

    // Verify all 3 frames and trailer frame unpack cleanly from the binary stream
    size_t offset = 0;
    size_t c1 = 0;
    auto f1 = Protocol::UnpackFrame(std::string_view(decoded_binary).substr(offset), &c1);
    ASSERT_TRUE(f1.has_value()) << "Failed to unpack frame 1. decoded_binary size="
                                << decoded_binary.size() << ", raw_b64='" << raw_b64
                                << "', received_data='" << received_data << "'";
    EXPECT_EQ(f1->flag, kFrameData);
    EXPECT_EQ(f1->payload, "Payload1");
    offset += c1;

    size_t c2 = 0;
    auto f2 = Protocol::UnpackFrame(std::string_view(decoded_binary).substr(offset), &c2);
    ASSERT_TRUE(f2.has_value());
    EXPECT_EQ(f2->flag, kFrameData);
    EXPECT_EQ(f2->payload, "Payload");
    offset += c2;

    size_t c3 = 0;
    auto f3 = Protocol::UnpackFrame(std::string_view(decoded_binary).substr(offset), &c3);
    ASSERT_TRUE(f3.has_value());
    EXPECT_EQ(f3->flag, kFrameData);
    EXPECT_EQ(f3->payload, "PayloadThree");
    offset += c3;

    size_t c4 = 0;
    auto f4 = Protocol::UnpackFrame(std::string_view(decoded_binary).substr(offset), &c4);
    ASSERT_TRUE(f4.has_value());
    EXPECT_EQ(f4->flag, kFrameTrailer);
    EXPECT_TRUE(absl::StrContains(f4->payload, "grpc-status:0"));
}

TEST_F(GrpcWebServerTest, EndToEndGrpcCallWithConnectionClose) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (WaitForTag(reinterpret_cast<void*>(1))) {
            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (WaitForTag(reinterpret_cast<void*>(2))) {
                stream.Write(recv_bb, reinterpret_cast<void*>(3));
                WaitForTag(reinterpret_cast<void*>(3));
            }
            stream.Finish(grpc::Status::OK, reinterpret_cast<void*>(4));
            WaitForTag(reinterpret_cast<void*>(4));
        }
    });

    std::string test_payload = "Close-Header-Verification";
    std::string data_frame = Protocol::PackFrame(kFrameData, test_payload);
    std::string http_post =
            "POST /test.EchoService/Echo HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n"
            "Origin: http://localhost:3000\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(data_frame.size()) +
            "\r\n"
            "\r\n" +
            data_frame;

    absl::Notification response_received;
    std::string received_data;

    async::ScopedAsyncSocket client(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                if (received_data.find("grpc-status:0") != std::string::npos) {
                    if (!response_received.HasBeenNotified()) {
                        response_received.Notify();
                    }
                }
            }
        });
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(response_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    PostAndWait([&]() {
        client->Close();
        server->Stop();
    });
    responder.join();

    EXPECT_NE(received_data.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(received_data.find("access-control-allow-origin: http://localhost:3000"),
              std::string::npos);
}

TEST_F(GrpcWebServerTest, HttpKeepAliveMultipleRequests) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    std::string req1 =
            "OPTIONS /test.Service/Method1 HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: http://localhost:3000\r\n"
            "Access-Control-Request-Method: POST\r\n\r\n";

    std::string req2 =
            "OPTIONS /test.Service/Method2 HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Origin: http://localhost:3000\r\n"
            "Access-Control-Request-Method: POST\r\n\r\n";

    absl::Notification two_responses_received;
    std::string received_data;
    std::shared_ptr<async::AsyncSocket> client_sock;

    PostAndWait([&]() {
        client_sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        client_sock->SetOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
            if (err.ok()) {
                received_data.append(data);
                size_t first = received_data.find("HTTP/1.1 204 No Content");
                if (first != std::string::npos) {
                    size_t second = received_data.find("HTTP/1.1 204 No Content", first + 1);
                    if (second != std::string::npos) {
                        if (!two_responses_received.HasBeenNotified()) {
                            two_responses_received.Notify();
                        }
                    }
                }
            }
        });
        client_sock->SetOnConnectedCallback([req1](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(req1.data(), req1.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(client_sock->Connect().ok());
    });

    // Wait for first response or short delay, then send req2
    std::this_thread::sleep_for(50ms);
    PostAndWait([&]() { client_sock->Send(req2.data(), req2.size()).IgnoreError(); });

    ASSERT_TRUE(two_responses_received.WaitForNotificationWithTimeout(absl::Seconds(5)));

    PostAndWait([&]() {
        client_sock->Close();
        server->Stop();
    });
}

TEST_F(GrpcWebServerTest, ClientDisconnectCancelsActiveStreamingCall) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);
    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    absl::Notification stream_started;
    absl::Notification stream_cancelled;

    // Server-side streaming RPC handler
    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);
        void* tag = nullptr;
        bool ok = false;

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (cq_->Next(&tag, &ok) && ok) {
            server_ctx.AsyncNotifyWhenDone(reinterpret_cast<void*>(5));
            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (cq_->Next(&tag, &ok) && ok) {
                stream_started.Notify();
                // Send continuous frames until cancelled by client disconnect
                std::string frame = Protocol::PackFrame(kFrameData, "stream-frame");
                grpc::Slice slice(frame.data(), frame.size());
                grpc::ByteBuffer write_bb(&slice, 1);

                bool done_received = false;
                bool write_in_flight = false;
                while (!done_received) {
                    if (!write_in_flight) {
                        std::this_thread::sleep_for(30ms);
                        stream.Write(write_bb, reinterpret_cast<void*>(3));
                        write_in_flight = true;
                    }
                    if (!cq_->Next(&tag, &ok)) {
                        break;
                    }
                    if (tag == reinterpret_cast<void*>(5)) {
                        // Cancellation / Done notification from gRPC
                        done_received = true;
                        stream_cancelled.Notify();
                    } else if (tag == reinterpret_cast<void*>(3)) {
                        write_in_flight = false;
                        if (!ok) {
                            done_received = true;
                            stream_cancelled.Notify();
                        }
                    }
                }

                while (write_in_flight) {
                    if (!cq_->Next(&tag, &ok)) {
                        break;
                    }
                    if (tag == reinterpret_cast<void*>(3)) {
                        write_in_flight = false;
                    }
                }
            }
        }
    });

    std::string test_payload = "ping";
    std::string data_frame = Protocol::PackFrame(kFrameData, test_payload);
    std::string http_post =
            "POST /test.StreamService/Stream HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(data_frame.size()) +
            "\r\n"
            "\r\n" +
            data_frame;

    std::shared_ptr<async::AsyncSocket> client_sock;
    PostAndWait([&]() {
        client_sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        client_sock->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        client_sock->SetOnConnectedCallback(
                [http_post](async::AsyncSocket& s, absl::Status status) {
                    if (status.ok()) {
                        s.Send(http_post.data(), http_post.size()).IgnoreError();
                    }
                });
        EXPECT_TRUE(client_sock->Connect().ok());
    });

    // Wait for the stream to start
    ASSERT_TRUE(stream_started.WaitForNotificationWithTimeout(absl::Seconds(5)));

    // Verify session is active
    EXPECT_GE(server->ActiveSessionsCount(), 1u);

    // Now close/disconnect the client socket abruptly (simulating Ctrl+C on curl)
    PostAndWait([&]() { client_sock->Close(); });

    // Verify upstream gRPC stream was promptly cancelled
    EXPECT_TRUE(stream_cancelled.WaitForNotificationWithTimeout(absl::Seconds(15)));

    // Verify session was removed from server active_sessions
    bool sessions_cleared = false;
    for (int i = 0; i < 100; ++i) {
        if (server->ActiveSessionsCount() == 0) {
            sessions_cleared = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_TRUE(sessions_cleared);

    if (responder.joinable()) {
        responder.join();
    }

    PostAndWait([&]() { server->Stop(); });
}

TEST_F(GrpcWebServerTest, AccessLoggingCallbackInvoked) {
    auto endpoint = network::ToEndpoint(network::ToIpAddress("127.0.0.1").value(), 0);

    absl::Notification log_received;
    http::AccessLogEntry logged_entry;

    auto access_logger = [&](const http::AccessLogEntry& entry) {
        logged_entry = entry;
        if (!log_received.HasBeenNotified()) {
            log_received.Notify();
        }
    };

    auto server_or = PostAndWait([&]() {
        return GrpcWebServer::Create(raw_loop_, socket_factory_, endpoint, grpc_channel_, "*",
                                     access_logger);
    });
    ASSERT_TRUE(server_or.ok());
    auto server = std::move(*server_or);
    auto listen_ep = PostAndWait([&]() { return server->GetEndpoint(); });

    std::thread responder([&]() {
        grpc::GenericServerContext server_ctx;
        grpc::GenericServerAsyncReaderWriter stream(&server_ctx);

        generic_service_.RequestCall(&server_ctx, &stream, cq_.get(), cq_.get(),
                                     reinterpret_cast<void*>(1));
        if (WaitForTag(reinterpret_cast<void*>(1))) {
            grpc::ByteBuffer recv_bb;
            stream.Read(&recv_bb, reinterpret_cast<void*>(2));
            if (WaitForTag(reinterpret_cast<void*>(2))) {
                stream.Write(recv_bb, reinterpret_cast<void*>(3));
                WaitForTag(reinterpret_cast<void*>(3));
            }
            stream.Finish(grpc::Status::OK, reinterpret_cast<void*>(4));
            WaitForTag(reinterpret_cast<void*>(4));
        }
    });

    std::string echo_payload = "AccessLogTest";
    std::string data_frame = Protocol::PackFrame(kFrameData, echo_payload);
    std::string http_post =
            "POST /test.EchoService/Echo HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/grpc-web+proto\r\n"
            "Content-Length: " +
            std::to_string(data_frame.size()) + "\r\n\r\n" + data_frame;

    async::ScopedAsyncSocket client_sock(PostAndWait([&]() {
        auto sock = socket_factory_->CreateSocket(raw_loop_, listen_ep);
        sock->SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
        sock->SetOnConnectedCallback([http_post](async::AsyncSocket& s, absl::Status status) {
            if (status.ok()) {
                s.Send(http_post.data(), http_post.size()).IgnoreError();
            }
        });
        EXPECT_TRUE(sock->Connect().ok());
        return sock;
    }));

    ASSERT_TRUE(log_received.WaitForNotificationWithTimeout(absl::Seconds(5)));
    EXPECT_EQ(logged_entry.path, "/test.EchoService/Echo");
    EXPECT_EQ(logged_entry.status_code, 200);
    EXPECT_GT(logged_entry.bytes_sent, 0u);

    if (responder.joinable()) {
        responder.join();
    }

    PostAndWait([&]() {
        client_sock->Close();
        server->Stop();
    });
}

}  // namespace goldfish::grpcweb
