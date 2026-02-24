// Copyright (C) 2025 The Android Open Source Project
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
#pragma once

#include "gmock/gmock.h"

#include "goldfish/async/async_socket.h"

namespace goldfish::async::testing {

/**
 * @brief A fake implementation of `AsyncSocket` for testing.
 *
 * This class is a hybrid object that combines Google Mock's mocking
 * capabilities with a manually implemented fake for simulating socket behavior.
 * By default, it behaves like a fake, automatically capturing callbacks.
 *
 * This allows you to:
 * 1.  Set expectations on method calls using `EXPECT_CALL`.
 * 2.  Trigger asynchronous events like reads, errors, and disconnections
 *     using the `Simulate...` methods.
 * 3.  Rely on the default fake behavior for methods where no expectation is set.
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/async/testing/fake_async_socket.h"
 * #include "goldfish/async/testing/fake_event_loop.h"
 *
 * TEST(MyTest, SocketRead) {
 *   FakeEventLoop loop;
 *   auto socket = std::make_shared<FakeAsyncSocket>();
 *   socket->setEventLoop(&loop);
 *
 *   std::string received_data;
 *   absl::Status read_status;
 *
 *   // Set the callback on the socket. The fake implementation will store it.
 *   socket->SetOnReadCallbackNoFlowControl([&](std::string_view data,
 * absl::Status status) {
 *     received_data = data;
 *     read_status = status;
 *   });
 *
 *   // Simulate the server sending data to the client
 *   socket->SimulateRead("hello");
 *
 *   // The callback is posted to the event loop, so we need to run it.
 *   loop.runAvailable();
 *
 *   EXPECT_EQ(received_data, "hello");
 *   EXPECT_TRUE(read_status.ok());
 * }
 * @endcode
 */
class FakeAsyncSocket : public AsyncSocket {
  public:
    FakeAsyncSocket() {
        ON_CALL(*this, SetOnReadCallbackNoFlowControl(::testing::_))
                .WillByDefault([this](OnReadCallback cb) { read_cb_ = std::move(cb); });
        ON_CALL(*this, SetOnCloseCallback(::testing::_)).WillByDefault([this](OnCloseCallback cb) {
            close_cb_ = std::move(cb);
        });
        ON_CALL(*this, SetOnConnectedCallback(::testing::_))
                .WillByDefault([this](OnConnectCallback cb) { connected_cb_ = std::move(cb); });
        ON_CALL(*this, GetLoop()).WillByDefault([this]() { return event_loop_; });
    }

    MOCK_METHOD(void, SetOnReadCallbackNoFlowControl, (OnReadCallback cb), (override));
    MOCK_METHOD(void, OnFlowControlEvent, (bool enableReading), (override));
    MOCK_METHOD(void, SetOnCloseCallback, (OnCloseCallback cb), (override));
    MOCK_METHOD(void, SetOnConnectedCallback, (OnConnectCallback cb), (override));

    MOCK_METHOD(absl::Status, Send, (const char* buffer, size_t buffer_size, OnSendCallback cb),
                (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(absl::Status, Connect, (), (override));
    MOCK_METHOD(bool, Connected, (), (const, override));
    MOCK_METHOD(EventLoop*, GetLoop, (), (const, override));

    /**
     * @brief Sets the event loop this fake socket should use.
     *
     * This is required for the `Simulate...` methods to post callbacks
     * back to the correct event loop.
     *
     * @param loop A non-owning pointer to the event loop.
     */
    void setEventLoop(goldfish::async::EventLoop* loop) { event_loop_ = loop; }

    /**
     * @brief Simulates an asynchronous read event with success.
     *
     * Posts the `OnReadCallback` to the event loop with the provided data
     * and an `absl::OkStatus()`. The callback must have been set previously,
     * either via `SetOnReadCallbackNoFlowControl` or by direct assignment to
     * `read_cb_`.
     *
     * @param d The data to deliver to the read callback.
     */
    void SimulateRead(std::string_view d) {
        if (read_cb_)
            (void)event_loop_->Post(
                    [data = std::string(d), this] { read_cb_(data, absl::OkStatus()); });
    }

    /**
     * @brief Simulates an asynchronous read event with an error.
     *
     * Posts the `OnReadCallback` to the event loop with an empty string_view
     * and the provided error status.
     *
     * @param err The error status to deliver to the read callback.
     */
    void SimulateError(absl::Status err) {
        if (read_cb_) (void)event_loop_->Post([err, this] { read_cb_("", err); });
    }

    /**
     * @brief Simulates the socket being closed.
     *
     * Posts the `OnCloseCallback` to the event loop.
     */
    void SimulateClose() {
        if (close_cb_) (void)event_loop_->Post([this] { close_cb_(); });
    }

    /**
     * @brief Simulates the completion of a connection attempt.
     *
     * Posts the `OnConnectCallback` to the event loop with the given status.
     *
     * @param s The status of the connection attempt (e.g.,
     * `absl::OkStatus()`).
     */
    void SimulateConnected(absl::Status s) {
        if (connected_cb_) (void)event_loop_->Post([this, s] { connected_cb_(*this, s); });
    }

    /// @brief The stored `OnReadCallback`.
    OnReadCallback read_cb_;
    /// @brief The stored `OnCloseCallback`.
    OnCloseCallback close_cb_;
    /// @brief The stored `OnConnectCallback`.
    OnConnectCallback connected_cb_;
    /// @brief A non-owning pointer to the `EventLoop` for posting events.
    goldfish::async::EventLoop* event_loop_ = nullptr;
};

/**
 * @brief A Google Mock implementation of `AsyncSocketFactory`.
 *
 * Use this mock in tests to control the creation of `AsyncSocket` and
 * `AsyncSocketServer` instances, allowing you to inject `FakeAsyncSocket`
 * or other test-specific implementations.
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/async/testing/fake_async_socket.h"
 *
 * TEST(MyServiceTest, ConnectsSuccessfully) {
 *   auto factory = std::make_shared<MockAsyncSocketFactory>();
 *   auto socket = std::make_shared<FakeAsyncSocket>();
 *
 *   // Expect that the service will ask for a socket, and return our fake.
 *   EXPECT_CALL(*factory, CreateSocket(testing::_, testing::_))
 *       .WillOnce(testing::Return(socket));
 *
 *   MyService service(factory);
 *   service.start();
 *
 *   // ... rest of test
 * }
 * @endcode
 */
class MockAsyncSocketFactory : public AsyncSocketFactory {
  public:
    MOCK_METHOD(std::shared_ptr<AsyncSocketServer>, CreateServer,
                (EventLoop*, const network::Endpoint&, AsyncSocketServer::ConnectCallback),
                (override));
    MOCK_METHOD(std::shared_ptr<AsyncSocket>, CreateSocket, (EventLoop*, const network::Endpoint&),
                (override));
};

}  // namespace goldfish::async::testing