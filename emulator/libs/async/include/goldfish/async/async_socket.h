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

#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/scoped_async_resource.h"

namespace goldfish::async {

/**
 * @brief Defines the interface for an asynchronous, event-driven stream socket.
 *
 * This abstract class provides the core contract for non-blocking socket
 * operations. All I/O is performed asynchronously, with results and data
 * delivered via user-provided callbacks.
 *
 * Instances of this class are created via an `AsyncSocketFactory`.
 *
 * @warning **Threading Model:**
 * All methods of an `AsyncSocket` instance **MUST** be called from the
 * `EventLoop` thread it is associated with. This design guarantees thread
 * safety without requiring manual locks. To interact with a socket from an
 * external thread, you must delegate the call using `EventLoop::post()`.
 *
 * @warning **Object Lifetime:**
 * The following rules must be observed:
 * 1.  The `EventLoop` object must outlive any `AsyncSocket` associated with it.
 * 2.  The `AsyncSocket::close()` method must be called before the object is
 * destroyed. Failure to do so will result in an assertion failure in
 * debug builds.
 *
 * The easiest and safest way to manage a socket's lifetime is to use the
 * `ScopedAsyncSocket` RAII wrapper, which automatically handles closing the
 * socket when it goes out of scope.
 *
 * @section usage_example Usage Example
 *
 * The following example demonstrates how to create a client socket, connect to a
 * server, and receive data, following the correct threading and lifetime patterns.
 *
 * @code
 * #include "goldfish/async/libuv_event_loop.h"
 * #include "goldfish/async/libuv_socket_factory.h"
 * #include "goldfish/async/scoped_async_socket.h"
 * #include "absl/synchronization/notification.h"
 * #include <thread>
 * #include <iostream>
 *
 * void run_client_example() {
 * // 1. Create an event loop and run it in a background thread.
 * auto loop = std::make_unique<goldfish::async::LibuvEventLoop>();
 * std::thread loop_thread([&]() { loop->run(); });
 *
 * auto factory = std::make_unique<goldfish::async::LibuvAsyncSocketFactory>();
 * absl::Notification message_received;
 *
 * // 2. Create the socket on the loop thread and wrap it in a ScopedAsyncSocket.
 * goldfish::async::ScopedAsyncSocket client(loop->postAndWait([&] {
 * return factory->createSocket(loop.get(), "127.0.0.1:8080");
 * }));
 *
 * // 3. Post tasks to the loop to configure and use the socket.
 * loop->post([&]() {
 * client->setOnReadCallbackNoFlowControl([&](std::string_view data, absl::Status err) {
 * if (!err.ok()) {
 * std::cerr << "Read error: " << err << std::endl;
 * return;
 * }
 * std::cout << "Received data: " << data << std::endl;
 * message_received.Notify();
 * });
 *
 * client->setOnConnectedCallback([&](absl::Status status) {
 * if (!status.ok()) {
 * std::cerr << "Connection failed: " << status << std::endl;
 * return;
 * }
 * std::cout << "Connection established!" << std::endl;
 * client->send("hello");
 * });
 *
 * client->setOnCloseCallback([&]() {
 * std::cout << "Socket closed." << std::endl;
 * });
 *
 * // Initiate the connection.
 * client->Connect();
 * });
 *
 * // 4. Wait on the main thread for the operation to complete.
 * message_received.WaitForNotification();
 *
 * // 5. Cleanly shut down the loop. The client socket is closed automatically
 * //    by the ScopedAsyncSocket's destructor when this function returns.
 * loop->stop();
 * loop_thread.join();
 * }
 * @endcode
 */
class AsyncSocket {
  public:
    virtual ~AsyncSocket() = default;

    /// @brief Callback for read events. If `err` is not OK, `data` is empty.
    using OnReadCallback = std::function<void(std::string_view data, absl::Status err)>;
    /// @brief Callback for the result of a send operation.
    using OnSendCallback = std::function<void(absl::Status err)>;
    /// @brief Callback for when a socket is fully closed.
    using OnCloseCallback = std::function<void()>;

    /**
     * @brief Callback for the result of a connection attempt.
     *
     * This callback **MUST** set the reading callback in `sock` to prevent loss of data.
     * The process will abort intentionally otherwise.
     *
     * It is a good idea to set all the required callbacks (see above) in this one.
     *
     * @param sock The socket which just connected.
     * @param err The connect call result.
     * @warning This method must be called from the socket's event loop thread.
     */
    using OnConnectCallback = std::function<void(AsyncSocket& sock, absl::Status err)>;

    /**
     * @brief Sets the callback for read events.
     *
     * The provided callback will be executed on the socket's event loop thread.
     * If the `err` parameter is not `absl::OkStatus()`, a read error occurred,
     * and the `data` parameter will be an empty view.
     *
     * @param on_read The function to call with incoming data or a read error.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual void SetOnReadCallbackNoFlowControl(OnReadCallback on_read) = 0;

    /**
     * @brief Signals a flow control event to the socket.
     *
     * This method can be used to temporarily pause or resume reading from the
     * socket.
     *
     * @param enable_reading True to resume reading, false to pause.
     * @note This method is thread-safe.
     */
    virtual void OnFlowControlEvent(bool enable_reading) = 0;

    /**
     * @brief Sets the callback for when the socket is fully closed.
     *
     * The provided callback will be executed on the socket's event loop thread
     * after a close operation, initiated by `Close()` or a fatal error, has
     * completed.
     *
     * @param on_close The function to call upon closure.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual void SetOnCloseCallback(OnCloseCallback on_close) = 0;

    /**
     * @brief Sets the callback for a connection attempt.
     *
     * The provided callback will be executed on the socket's event loop thread
     * with a status indicating the success or failure of the connection attempt.
     *
     * @param on_connected The function to call with the connection result.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual void SetOnConnectedCallback(OnConnectCallback on_connected) = 0;

    /**
     * @brief Asynchronously sends a buffer of data over the socket.
     *
     * The provided data is copied internally, so the caller can safely reuse or
     * discard the original buffer immediately after this call returns.
     *
     * @param buffer A pointer to the data to be sent.
     * @param buffer_size The number of bytes to send from the buffer.
     * @param on_send The callback to be invoked upon completion of the send. This
     * callback will be executed on the socket's event loop thread.
     * @return absl::OkStatus() if the send was successfully queued, or an
     * error status on immediate failure.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual absl::Status Send(const char* buffer, size_t buffer_size, OnSendCallback on_send) = 0;

    /**
     * @brief A convenience overload for send that performs a "fire-and-forget"
     * operation without a completion callback.
     * @warning This method must be called from the socket's event loop thread.
     */
    absl::Status Send(const char* buffer, size_t buffer_size) {
        return Send(buffer, buffer_size, [](const auto&) {});
    }

    /**
     * @brief Initiates the asynchronous closing of the socket.
     *
     * The `OnCloseCallback` will be invoked when the operation is complete. This
     * method must be called before the socket object is destroyed. Using the
     * `ScopedAsyncSocket` RAII wrapper is the recommended way to ensure this.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual void Close() = 0;

    /**
     * @brief Asynchronously initiates a connection to the configured endpoint.
     *
     * The `OnConnectCallback` will be invoked to signal completion, providing an
     * `absl::Status` to indicate success or failure.
     *
     * @return absl::OkStatus() if the connection attempt was started, or an
     * error status on immediate failure.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual absl::Status Connect() = 0;

    /**
     * @brief Checks the current connection state of the socket.
     * @return true if the socket is currently connected and active, false
     * otherwise.
     * @warning This method must be called from the socket's event loop thread.
     */
    virtual bool Connected() const = 0;

    /**
     * @brief Returns the EventLoop this socket is bound to.
     * @return A non-owning pointer to the event loop.
     * @note This method is thread-safe and is the primary way for an external
     * thread to get the loop pointer needed to `post()` tasks.
     */
    virtual EventLoop* GetLoop() const = 0;

  protected:
    /**
     * @brief Provides a string representation for logging and debugging.
     *
     * This is the implementation hook for `absl::StrFormat`. Concrete socket
     * implementations should override this method to provide meaningful
     * diagnostic information (e.g., connection state, peer address).
     *
     * @param s The `absl::FormatSink` to write the formatted string to.
     */
    virtual void AbslStringifyImpl(absl::FormatSink& s) const {
        absl::Format(&s, "<DefaultAsyncSocket loop=%p>", GetLoop());
    }

  private:
    friend void AbslStringify(absl::FormatSink& s, const AsyncSocket& socket);
};

using ScopedAsyncSocket = ScopedAsyncResource<AsyncSocket>;

/**
 * @brief Enables `absl::StrFormat` support for `AsyncSocket`.
 *
 * This free function is the customization point that allows `AsyncSocket`
 * objects (and their derivatives) to be formatted with `absl::StrFormat`
 * using the `%v` format specifier. It delegates the actual formatting to
 * the virtual `AbslStringifyImpl` method.
 *
 * @param s The `absl::FormatSink` to write to.
 * @param socket The `AsyncSocket` to format.
 */
inline void AbslStringify(absl::FormatSink& s, const AsyncSocket& socket) {
    socket.AbslStringifyImpl(s);
}

/**
 * @brief Enables `std::ostream` support for `AsyncSocket`.
 *
 * This overload allows `AsyncSocket` objects to be streamed directly to any
 * `std::ostream` (e.g., `std::cout`, `std::stringstream`, or Abseil's `VLOG`).
 * It works by using `absl::StreamFormat` to delegate the formatting to the
 * `AbslStringify` customization point, ensuring a consistent string
 * representation.
 *
 * @param os The output stream to write to.
 * @param socket The `AsyncSocket` to format.
 * @return A reference to the output stream.
 */
std::ostream& operator<<(std::ostream& os, const AsyncSocket& socket);
// TODO: Add support for co-routines?

}  // namespace goldfish::async
