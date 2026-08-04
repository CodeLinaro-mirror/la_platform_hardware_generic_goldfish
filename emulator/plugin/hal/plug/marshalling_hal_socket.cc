/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/devices/marshalling_hal_socket.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::devices {

namespace {

// A socket that sends things nowhere..
struct NullSocket : public cable::ISocket {
    ~NullSocket() override = default;
    void SendAsync(const void* /*data*/, size_t size) override {
        VLOG(2) << "Sending " << size << " bytes to /dev/null";
    };

    cable::PlugPtr SwitchPlug(cable::PlugPtr new_plug) override {
        LOG(FATAL) << "NullSocket::SwitchPlug should not be called";
        return new_plug;
    }

    cable::PlugPtr UnplugImpl() override { return {}; }

    void AbslStringifyImpl(absl::FormatSink& s) const override { absl::Format(&s, "[NullSocket]"); }
};

NullSocket g_null_socket;
}  // namespace

MarshallingHalSocket::MarshallingHalSocket(cable::SocketPtr socket, async::EventLoop* qemu_loop)
        : socket_(std::move(socket)), qemu_loop_(qemu_loop) {
    DCHECK(socket_) << "socket_ is nullptr";
    send_buffer_.reserve(kInitialBufferSize);
    VLOG(1) << "MarshallingHalSocket: " << socket_ << " created";
}

MarshallingHalSocket::~MarshallingHalSocket() {
    VLOG(1) << "~MarshallingHalSocket: " << socket_ << " destroyed";
    if (!is_closed_) {
        LOG(WARNING) << "Inner socket was not closed!";
    }
}

void MarshallingHalSocket::Send(std::string data) {
    if (is_closed_) {
        VLOG(2) << "Dropping packet, socket is closed.";
        return;
    }

    bool schedule_flush = false;
    goldfish::async::StackAddress calling_pc = 0;

    {
        const absl::MutexLock lock(&socket_mutex_);
        schedule_flush = send_buffer_.empty();
        send_buffer_.append(data);

        if (schedule_flush) {
            // Note: During write-buffer coalescing, we record the instruction pointer
            // (calling_pc) of the first Send() call that initiates the batch flush task.
            calling_pc = __builtin_return_address(0);
        } else {
            VLOG(2) << "Appended " << data.size()
                    << " bytes to pending coalesced buffer (total: " << send_buffer_.size()
                    << " bytes)";
            return;
        }
    }

    // Post the coalesced send operation to the QEMU loop asynchronously.
    qemu_loop_
            ->Post(
                    [this, self = shared_from_this()]() {
                        std::string payload_to_send;
                        {
                            const absl::MutexLock lock(&socket_mutex_);
                            if (send_buffer_.empty()) {
                                return;
                            }
                            payload_to_send = std::move(send_buffer_);
                            send_buffer_.reserve(kInitialBufferSize);

                            VLOG(2) << "Sending coalesced payload of " << payload_to_send.size()
                                    << " bytes";
                            // Bytes go either to the *real* or NullSocket..
                            socket_->SendAsync(payload_to_send.data(), payload_to_send.size());
                        }
                    },
                    std::chrono::milliseconds::zero(), {.caller_pc = calling_pc})
            .IgnoreError();
}

void MarshallingHalSocket::AbslStringifyImpl(absl::FormatSink& s) const {
    absl::Format(&s, "[MarshallingHalSocket %s]", is_closed_ ? "closed" : "open");
}

cable::SocketPtr MarshallingHalSocket::Release() {
    const absl::MutexLock lock(&socket_mutex_);
    VLOG(1) << "Releasing the socket.";
    auto s = std::move(socket_);
    socket_ = cable::SocketPtr(&g_null_socket);
    return s;
}

void MarshallingHalSocket::Close() {
    if (!is_closed_.exchange(true)) {
        VLOG(1) << "Closing the socket.";
        // Post the unplug operation to the QEMU loop asynchronously.
        // This avoids deadlocking if close() is called from a client
        // callback that was initiated by the QEMU loop.
        qemu_loop_
                ->Post([this, self = shared_from_this()]() {
                    VLOG(1) << "Calling onplug on socket: " << *this;
                    cable::SocketPtr socket_to_unplug;
                    {
                        // Safely take ownership of the real socket pointer
                        // under the lock. This coordinates with the release()
                        // method, which may be called by onUnplug on this same
                        // QEMU thread.
                        const absl::MutexLock lock(&socket_mutex_);

                        // Don't unplug if it was already the null socket (e.g., if
                        // release() was called first).
                        if (socket_.get() != &g_null_socket) {
                            socket_to_unplug = std::move(socket_);
                            socket_ = cable::SocketPtr(&g_null_socket);
                        }
                    }

                    // Unplug the real socket outside the lock.
                    if (socket_to_unplug) {
                        VLOG(1) << "Unplugging the real socket.";
                        cable::ISocket::Unplug(std::move(socket_to_unplug));
                    }
                })
                .IgnoreError();
    } else {
        VLOG(1) << "Socket already closed";
    }
}
}  // namespace goldfish::devices
