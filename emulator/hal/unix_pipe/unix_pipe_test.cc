// Copyright (C) 2025 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "goldfish/devices/unix_pipe/unix_pipe.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/mutex.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/base/testing/TestSystem.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/network/un_endpoint.h"

namespace goldfish::devices::unix_pipe {

using absl_testing::IsOk;
using testing::Not;

using android::base::TestSystem;
using async::testing::TestEventLoop;
using goldfish::async::AsyncSocket;
using goldfish::async::LibuvAsyncSocketFactory;
using goldfish::async::LibuvEventLoop;
using goldfish::async::ScopedAsyncServer;
using goldfish::async::ScopedAsyncSocket;
using goldfish::network::Endpoint;
using goldfish::network::UnEndpoint;

using namespace std::chrono_literals;
using namespace std::literals::string_view_literals;

class UnixPipeTest : public ::testing::Test {
    void SetUp() override {
        client_loop_ = LibuvEventLoop::Create();
        client_loop_thread_ = std::thread([this] { (void)client_loop_->Run(); });
        qemu_loop_ = TestEventLoop::create();
        IUnixPipe::RegisterDevice(&registry_, client_loop_.get(), qemu_loop_.get());
    }

    void TearDown() override {
        auto s = client_loop_->ShutdownAndWait(100ms);
        ASSERT_THAT(s, absl_testing::IsOk());
        if (client_loop_thread_.joinable()) {
            client_loop_thread_.join();
        }
    }

  public:
    template <typename F>
    auto PostAndWait(F&& func) -> decltype(func()) {
        auto res = client_loop_->PostAndWait(std::forward<F>(func));
        EXPECT_THAT(res, absl_testing::IsOk());
        if constexpr (std::is_void_v<decltype(func())>) {
            return;
        } else {
            return *res;
        }
    }

    template <typename T>
    void RunUntil(std::future<T>& future, std::chrono::milliseconds timeout = 2s) {
        ASSERT_EQ(future.wait_for(timeout), std::future_status::ready);
    }

  protected:
    std::unique_ptr<LibuvEventLoop> client_loop_;
    std::thread client_loop_thread_;
    std::unique_ptr<TestEventLoop> qemu_loop_;
    TestConnectorRegistry registry_;
    LibuvAsyncSocketFactory socket_factory_;
};

TEST_F(UnixPipeTest, echo_over_host_side) {
#ifdef _WIN32
    const std::string un_path(absl::StrFormat("\\\\.\\pipe\\UnixPipeTest_echo_over_host_side_%d",
                                              GetCurrentProcessId()));
#else
    android::base::TestTempDir tmpdir("UnixPipeTest");
    const std::filesystem::path un_path = tmpdir.makeSubPath("echo_over_host_side");
#endif

    const auto endpoint = Endpoint(*UnEndpoint::Create(un_path));

    std::vector<ScopedAsyncSocket> clients;
    std::mutex clients_mutex;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->SetOnReadCallbackNoFlowControl(
                [socket](std::string_view data, absl::Status /*err*/) {
                    ASSERT_THAT(socket->Send(data.data(), data.size()), IsOk());
                });

        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.emplace_back(std::move(socket));
        return true;
    };

    const ScopedAsyncServer server(PostAndWait([this, &endpoint, &on_connect] {
        return socket_factory_.CreateServer(client_loop_.get(), endpoint, std::move(on_connect));
    }));
    ASSERT_NE(server, nullptr);

    const ScopedAsyncSocket client(PostAndWait([this, &endpoint] {
        return socket_factory_.CreateSocket(client_loop_.get(), endpoint);
    }));
    ASSERT_NE(client, nullptr);

    const std::string test_message = "test message";

    std::promise<std::string> echo_promise;
    auto echo_future = echo_promise.get_future();
    std::string rx_buffer;

    client_loop_
            ->Post([&]() {
                client->SetOnReadCallbackNoFlowControl(
                        [&](const std::string_view data, const absl::Status /*err*/) {
                            rx_buffer += std::string(data);
                            if (rx_buffer.size() == test_message.size()) {
                                echo_promise.set_value(std::move(rx_buffer));
                            }
                        });

                client->SetOnConnectedCallback([&test_message](AsyncSocket& socket,
                                                               absl::Status /*err*/) {
                    ASSERT_THAT(socket.Send(test_message.data(), test_message.size()), IsOk());
                });

                ASSERT_THAT(client->Connect(), IsOk());
            })
            .IgnoreError();

    RunUntil(echo_future);
    EXPECT_EQ(echo_future.get(), test_message);
}

struct ClientState {
    std::string rx_buffer;
    bool connected = true;

    bool ready() const { return !rx_buffer.empty() || !connected; }
};

TEST_F(UnixPipeTest, close_on_host) {
#ifdef _WIN32
    const std::string un_path(
            absl::StrFormat("\\\\.\\pipe\\UnixPipeTest_%d", GetCurrentProcessId()));
#else
    android::base::TestTempDir tmpdir("UnixPipeTest");
    const std::filesystem::path un_path = tmpdir.makeSubPath("close_on_host");
#endif

    const auto endpoint = Endpoint(*UnEndpoint::Create(un_path));

    std::vector<ScopedAsyncSocket> clients;
    std::mutex clients_mutex;
    int server_replies = 3;

    auto on_connect = [&](std::shared_ptr<AsyncSocket> socket) -> bool {
        socket->SetOnReadCallbackNoFlowControl([socket, &server_replies](std::string_view data,
                                                                         absl::Status /*err*/) {
            if (server_replies > 0) {
                fprintf(stderr, "%s:%d: server_replies=%d data.size()=%zu\n",
                        "SetOnReadCallbackNoFlowControl", __LINE__, server_replies, data.size());

                --server_replies;
                ASSERT_THAT(socket->Send(data.data(), data.size()), IsOk());
            } else {
                fprintf(stderr, "%s:%d: closing\n", "SetOnReadCallbackNoFlowControl", __LINE__);
                socket->Close();
            }
        });

        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.emplace_back(std::move(socket));
        return true;
    };

    const ScopedAsyncServer server(PostAndWait([this, &endpoint, &on_connect] {
        return socket_factory_.CreateServer(client_loop_.get(), endpoint, std::move(on_connect));
    }));
    ASSERT_NE(server, nullptr);

    const ScopedAsyncSocket client(PostAndWait([this, &endpoint] {
        return socket_factory_.CreateSocket(client_loop_.get(), endpoint);
    }));
    ASSERT_NE(client, nullptr);

    std::string rx_total;

    absl::Mutex client_updated;
    ClientState client_state;

    {
        std::promise<void> client_connected_promise;
        auto client_connected_future = client_connected_promise.get_future();

        client_loop_
                ->Post([&]() {
                    client->SetOnReadCallbackNoFlowControl([&](const std::string_view data,
                                                               const absl::Status err) {
                        fprintf(stderr, "%s:%d\n", "SetOnReadCallbackNoFlowControl", __LINE__);
                        const absl::MutexLock lk(&client_updated);
                        if (err.ok()) {
                            client_state.rx_buffer += std::string(data);
                        }
                    });

                    client->SetOnConnectedCallback(
                            [&client_connected_promise](AsyncSocket& /*socket*/,
                                                        absl::Status /*err*/) {
                                client_connected_promise.set_value();
                            });

                    client->SetOnCloseCallback([&]() {
                        fprintf(stderr, "%s:%d\n", "SetOnCloseCallback", __LINE__);
                        const absl::MutexLock lk(&client_updated);
                        client_state.connected = false;
                    });

                    ASSERT_THAT(client->Connect(), IsOk());
                })
                .IgnoreError();

        RunUntil(client_connected_future);
    }

    const std::string test_message = "abc";

    for (int n = 3; n > 0; --n) {
        fprintf(stderr, "%s:%d good\n", "Send", __LINE__);
        client_loop_
                ->Post([&]() {
                    ASSERT_THAT(client->Send(test_message.data(), test_message.size()), IsOk());
                })
                .IgnoreError();

        const absl::MutexLock lk(&client_updated);
        ASSERT_TRUE(client_updated.AwaitWithTimeout(
                absl::Condition(
                        +[](const void* ptr) {
                            return static_cast<const ClientState*>(ptr)->ready();
                        },
                        &client_state),
                absl::FromChrono(1s)));

        rx_total += client_state.rx_buffer;
        client_state.rx_buffer.clear();
    }

    EXPECT_EQ(rx_total, "abcabcabc");

    client_loop_
            ->Post([&]() {
                ASSERT_THAT(client->Send(test_message.data(), test_message.size()), IsOk());
            })
            .IgnoreError();

    {
        const absl::MutexLock lk(&client_updated);
        ASSERT_TRUE(client_updated.AwaitWithTimeout(
                absl::Condition(
                        +[](const void* ptr) {
                            return static_cast<const ClientState*>(ptr)->ready();
                        },
                        &client_state),
                absl::FromChrono(1s)));

        ASSERT_TRUE(client_state.rx_buffer.empty());
    }
}

}  // namespace goldfish::devices::unix_pipe
