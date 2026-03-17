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

#include "console_server.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/network/ip_address.h"
#include "line_command_handler.h"

namespace goldfish::telnet {

/**
 * @brief Test peer giving inspecting access to ConsoleServer private internals.
 *
 * Provides static inspect scopes verifying into active loop connection map dimensions
 * and references safely just for unit assertions securely.
 */
class ConsoleServerPeer {
  public:
    struct ConnectionTracker {
        std::weak_ptr<ConsoleServer::Connection> conn;
        bool expired() const { return conn.expired(); }
    };

    static size_t GetActiveSessionsCount(const ConsoleServer& server) {
        absl::MutexLock lock(const_cast<absl::Mutex&>(server.mutex_));
        return server.active_sessions_.size();
    }
    static ConnectionTracker GetTracker(const ConsoleServer& server) {
        absl::MutexLock lock(const_cast<absl::Mutex&>(server.mutex_));
        if (server.active_sessions_.empty()) return {};
        return {server.active_sessions_.begin()->second.connection};
    }
    static Endpoint GetEndpoint(const ConsoleServer& server) {
        return server.server_->GetEndpoint();
    }
    static Endpoint MakeLoopbackEndpoint() {
        return goldfish::network::ToEndpoint(*goldfish::network::ToIpAddress("127.0.0.1"), 0);
    }

    struct WaitForSessionCountArgs {
        const ConsoleServer* server;
        size_t count;
    };

    static bool WaitForSessionCount(const ConsoleServer& server, size_t expected_count,
                                    absl::Duration timeout) {
        absl::Mutex& mutex = const_cast<absl::Mutex&>(server.mutex_);
        WaitForSessionCountArgs args{&server, expected_count};
        auto condition = +[](WaitForSessionCountArgs* arg) {
            return arg->server->active_sessions_.size() == arg->count;
        };
        absl::MutexLock lock(mutex);
        return mutex.AwaitWithTimeout(absl::Condition(condition, &args), timeout);
    }
};

namespace {

using ::goldfish::async::AsyncSocket;
using ::goldfish::async::LibuvAsyncSocketFactory;
using ::goldfish::async::LibuvEventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::network::Endpoint;

/**
 * @brief Simulates LineCommandHandler translations triggers for testing.
 *
 * Custom instructs:
 * - "echo": Returns "ok_echo"
 * - "hang": Sleeps loop 10s blocking execution paths testing boundary timeouts
 * - "abort": Triggers AbortedError testing premature closures
 */
class MockLineCommandHandler : public LineCommandHandler {
  public:
    absl::StatusOr<std::string> operator()(std::string line, Context& ctx) override {
        if (line == "echo") {
            return "ok_echo";
        }
        if (line == "hang") {
            VLOG(1) << "Hanging for 2 seconds";
            absl::SleepFor(absl::Seconds(2));
            VLOG(1) << "Done hanging";
            return "hung_done";
        }
        if (line == "abort") {
            return absl::AbortedError("aborting");
        }
        return absl::NotFoundError("unknown");
    }

    std::string WelcomeMessage(const Context& ctx) const override { return "Welcome"; }
};

/**
 * @brief Google Test Fixture centralizing EventLoop and Server context duplication.
 *
 * Manages ThreadedEventLoop setup, factory buffers allocation, and securely frees remaining
 * socket handlers on TearDown pipeline triggers.
 */
class ConsoleServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        loop_->PostAndWait([&]() {
                 factory_ = std::make_unique<LibuvAsyncSocketFactory>();
                 auto handler = std::make_shared<MockLineCommandHandler>();
                 Endpoint endpoint = ConsoleServerPeer::MakeLoopbackEndpoint();
                 server_ =
                         std::make_shared<ConsoleServer>(*factory_, loop_.get(), endpoint, handler);
             }).IgnoreError();
    }

    void TearDown() override {
        if (loop_) {
            VLOG(1) << "Tearing down: closing client";
            loop_->PostAndWait([&]() {
                     if (client_) client_->Close();
                 }).IgnoreError();

            if (server_) {
                VLOG(1) << "Tearing down: stopping server";
                // TSAN can be slow, so give it ample time for connections to close.
                server_->Stop(absl::Seconds(5)).IgnoreError();
                server_.reset();
            }

            VLOG(1) << "Tearing down: flushing loop and resetting factory";
            loop_->PostAndWait([&]() {
                     if (client_) client_.reset();
                     factory_.reset();
                 }).IgnoreError();
            VLOG(1) << "TearDown complete";
        }
    }

    void CreateClient() {
        client_ = factory_->CreateSocket(loop_.get(), ConsoleServerPeer::GetEndpoint(*server_));
        ASSERT_NE(client_, nullptr);
    }

    void WaitForSessionCount(size_t expected_count) {
        if (!ConsoleServerPeer::WaitForSessionCount(*server_, expected_count, absl::Seconds(5))) {
            FAIL() << "Timed out waiting for expected active session count " << expected_count;
        }
    }

    std::unique_ptr<ThreadedEventLoop> loop_;
    std::unique_ptr<LibuvAsyncSocketFactory> factory_;
    std::shared_ptr<ConsoleServer> server_;
    std::shared_ptr<AsyncSocket> client_;
};

TEST_F(ConsoleServerTest, ConstructionAndDestruction) {
    loop_->PostAndWait([&]() {
             auto status = server_->Start();
             EXPECT_TRUE(status.ok()) << status;
         }).IgnoreError();
}

TEST_F(ConsoleServerTest, StartAndStop) {
    loop_->PostAndWait([&]() {
             absl::Status status = server_->Start();
             EXPECT_TRUE(status.ok());

             status = server_->Start();
             EXPECT_FALSE(status.ok());
             EXPECT_EQ(status.code(), absl::StatusCode::kAlreadyExists);

             EXPECT_TRUE(server_->Stop().ok());
         }).IgnoreError();
}

TEST_F(ConsoleServerTest, OrganicLifecycle) {
    auto welcome_promise = std::make_shared<std::promise<void>>();
    auto welcome_future = welcome_promise->get_future();

    loop_->PostAndWait([&]() {
             EXPECT_TRUE(server_->Start().ok());
             CreateClient();

             client_->SetOnConnectedCallback(
                     [welcome_promise](AsyncSocket& sock, absl::Status status) {
                         EXPECT_TRUE(status.ok());
                         sock.SetOnReadCallbackNoFlowControl(
                                 [welcome_promise](std::string_view data, absl::Status err) {
                                     if (data.find("Welcome") != std::string::npos) {
                                         welcome_promise->set_value();
                                     }
                                 });
                     });
             EXPECT_TRUE(client_->Connect().ok());
         }).IgnoreError();

    // Wait for the server to fully establish session and send welcome message.
    EXPECT_EQ(welcome_future.wait_for(std::chrono::seconds(3)), std::future_status::ready);

    // Now close the client socket
    loop_->PostAndWait([&]() { client_->Close(); }).IgnoreError();

    // Wait for server to process client close and remove connection.
    WaitForSessionCount(0);

    size_t final_count;
    loop_->PostAndWait([&]() {
             final_count = ConsoleServerPeer::GetActiveSessionsCount(*server_);
         }).IgnoreError();
    EXPECT_EQ(final_count, 0);
}

TEST_F(ConsoleServerTest, CycleBreakerVerification_NoLeaks) {
    ConsoleServerPeer::ConnectionTracker tracker;

    loop_->PostAndWait([&]() {
             EXPECT_TRUE(server_->Start().ok());
             CreateClient();
             client_->SetOnConnectedCallback([](AsyncSocket& sock, absl::Status status) {
                 EXPECT_TRUE(status.ok());
                 sock.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
             });
             EXPECT_TRUE(client_->Connect().ok());
         }).IgnoreError();

    WaitForSessionCount(1);

    loop_->PostAndWait([&]() { tracker = ConsoleServerPeer::GetTracker(*server_); }).IgnoreError();

    EXPECT_FALSE(tracker.expired());

    loop_->PostAndWait([&]() { client_->Close(); }).IgnoreError();

    WaitForSessionCount(0);

    EXPECT_TRUE(tracker.expired())
            << "Memory leak detected! Circular references kept Connection alive.";
}

TEST_F(ConsoleServerTest, GracefulBoundedStop) {
    loop_->PostAndWait([&]() {
             EXPECT_TRUE(server_->Start().ok());
             CreateClient();
             client_->SetOnConnectedCallback([](AsyncSocket& sock, absl::Status status) {
                 EXPECT_TRUE(status.ok());
                 sock.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
             });
             EXPECT_TRUE(client_->Connect().ok());
         }).IgnoreError();

    WaitForSessionCount(1);

    auto start = absl::Now();
    EXPECT_TRUE(server_->Stop(absl::Seconds(3)).ok());
    auto duration = absl::Now() - start;

    EXPECT_LT(duration, absl::Seconds(1)) << "Graceful stop took too long, deadlocked?";
}

TEST_F(ConsoleServerTest, HostileForcedStop) {
    loop_->PostAndWait([&]() {
             EXPECT_TRUE(server_->Start().ok());
             CreateClient();
             client_->SetOnConnectedCallback([](AsyncSocket& sock, absl::Status status) {
                 if (!status.ok()) return;
                 sock.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
                 std::string payload = "hang\r\n";
                 sock.Send(payload.data(), payload.size(), [](absl::Status) {}).IgnoreError();
             });
             EXPECT_TRUE(client_->Connect().ok());
         }).IgnoreError();

    WaitForSessionCount(1);

    absl::SleepFor(absl::Milliseconds(100));

    auto start = absl::Now();
    VLOG(1) << "Stopping server...";
    auto status = server_->Stop(absl::Milliseconds(250));
    auto duration = absl::Now() - start;
    VLOG(1) << "Stop duration: " << duration;

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kDeadlineExceeded);

    EXPECT_GE(duration, absl::Milliseconds(250)) << "Stop didn't hit deadline constraint";
    EXPECT_LT(duration, absl::Seconds(9)) << "Stop deadlocked on hung thread!";
}

TEST_F(ConsoleServerTest, VerifyClientResponse) {
    auto echo_promise = std::make_shared<std::promise<bool>>();
    auto echo_future = echo_promise->get_future();

    std::string received_buffer;
    bool welcome_found = false;

    loop_->PostAndWait([&]() {
             EXPECT_TRUE(server_->Start().ok());
             CreateClient();

             std::weak_ptr<AsyncSocket> weak_client = client_;

             client_->SetOnConnectedCallback([&received_buffer, &welcome_found, echo_promise,
                                              weak_client](AsyncSocket& sock, absl::Status status) {
                 EXPECT_TRUE(status.ok()) << status;
                 if (!status.ok()) return;

                 sock.SetOnReadCallbackNoFlowControl([&received_buffer, &welcome_found,
                                                      echo_promise,
                                                      weak_client](std::string_view data,
                                                                   absl::Status err) {
                     EXPECT_TRUE(err.ok()) << err;
                     received_buffer.append(data);

                     if (!welcome_found &&
                         received_buffer.find("Welcome\r\n") != std::string::npos) {
                         welcome_found = true;
                         received_buffer.clear();
                         auto payload = std::make_shared<std::string>("echo\r\n");
                         auto lock = weak_client.lock();
                         if (lock) {
                             lock->Send(payload->data(), payload->size(),
                                        [payload](absl::Status snd_err) {})
                                     .IgnoreError();
                         }
                     } else if (welcome_found &&
                                received_buffer.find("ok_echo\r\nOK\r\n") != std::string::npos) {
                         echo_promise->set_value(true);
                     }
                 });
             });

             EXPECT_TRUE(client_->Connect().ok());
         }).IgnoreError();

    std::future_status status = echo_future.wait_for(std::chrono::seconds(3));
    EXPECT_EQ(status, std::future_status::ready);
    if (status == std::future_status::ready) {
        EXPECT_TRUE(echo_future.get());
    }

    auto close_promise = std::make_shared<std::promise<void>>();
    auto close_future = close_promise->get_future();

    loop_->PostAndWait([&]() {
             if (client_) {
                 client_->SetOnCloseCallback([close_promise]() { close_promise->set_value(); });
                 client_->Close();
             } else {
                 close_promise->set_value();
             }
         }).IgnoreError();

    std::future_status close_status = close_future.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(close_status, std::future_status::ready);

    WaitForSessionCount(0);
}

}  // namespace
}  // namespace goldfish::telnet
