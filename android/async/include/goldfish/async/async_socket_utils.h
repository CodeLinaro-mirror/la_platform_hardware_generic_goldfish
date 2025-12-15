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
#include <future>
#include <string_view>

#include "absl/status/status.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/event_loop.h"

namespace goldfish::async {
/**
 * @brief Sends data over an AsyncSocket and blocks the calling thread until
 * the send operation is complete.
 *
 * This function is a thread-safe bridge to the asynchronous send method. It
 * must not be called from the event loop's own thread.
 *
 * @param socket The socket to send data on.
 * @param data The data to send.
 * @return An absl::Status indicating the result of the send operation.
 */
inline absl::Status sendSynchronously(goldfish::async::AsyncSocket* socket, const char* data,
                                      size_t size) {
    // --- Validation ---
    if (!socket) {
        return absl::InvalidArgumentError("Socket must not be null.");
    }
    if (!data && size > 0) {
        return absl::InvalidArgumentError(
                "Data buffer cannot be null if size is greater than zero.");
    }

    if (size == 0) {
        return absl::OkStatus();
    }

    goldfish::async::EventLoop* loop = socket->GetLoop();
    if (!loop) {
        return absl::FailedPreconditionError("Socket is not associated with an event loop.");
    }
    if (loop->IsOnLoopThread()) {
        return absl::FailedPreconditionError(
                "sendSynchronously cannot be called from the event loop thread.");
    }

    auto promise_ptr = std::make_shared<std::promise<absl::Status>>();
    auto future = promise_ptr->get_future();

    loop->Post([socket, data, size, p = promise_ptr]() {
        socket->Send(data, size, [p](absl::Status status) { p->set_value(status); });
    });

    return future.get();
}

inline absl::Status sendSynchronously(goldfish::async::AsyncSocket* socket, std::string_view data) {
    return sendSynchronously(socket, data.data(), data.size());
}

inline absl::Status sendSynchronously(std::shared_ptr<AsyncSocket> socket, std::string_view data) {
    return sendSynchronously(socket.get(), data.data(), data.size());
}

inline absl::Status sendSynchronously(std::shared_ptr<AsyncSocket> socket, const char* data,
                                      size_t size) {
    return sendSynchronously(socket.get(), data, size);
}

}  // namespace goldfish::async