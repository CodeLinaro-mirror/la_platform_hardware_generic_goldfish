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
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"

#include <string_view>

#include "absl/log/log.h"

#include "goldfish/devices/marshalling_hal_socket.h"

namespace goldfish::devices {

HalPlugToIPlugAdapter::~HalPlugToIPlugAdapter() {
    VLOG(1) << "Tearing down " << *this << " with mHalPlug: " << *hal_plug_
            << ", use_count: " << hal_plug_.use_count();
}

HalPlugToIPlugAdapter::HalPlugToIPlugAdapter(async::EventLoop* client_loop,
                                             std::shared_ptr<HalPlug> hal_plug)
        : client_loop_(client_loop), hal_plug_(std::move(hal_plug)) {
    VLOG(1) << "HalPlugToIPlugAdapter created with mHalPlug: " << *hal_plug_
            << ", use_count: " << hal_plug_.use_count();
}

void HalPlugToIPlugAdapter::OnConnect() {
    // Let's inform the client of the new connection.
    VLOG(1) << "Scheduling OnConnect for mHalPlug: " << *hal_plug_;
    client_loop_->Post([plug = hal_plug_]() { plug->OnConnect(); }).IgnoreError();
}

bool HalPlugToIPlugAdapter::OnReceive(const void* data, size_t size) {
    // This is called on the QEMU thread.
    // We post the data to the client loop for the real HalPlug to handle.
    // We return true immediately, preventing the QEMU thread from blocking.
    //
    // This means that vsock will never close out this socket from this call.
    VLOG(2) << "Scheduling onReceive for mHalPlug " << *hal_plug_ << " with: " << size << " bytes.";
    client_loop_->Post([plug = hal_plug_, s = std::string(static_cast<const char*>(data), size)]() {
        plug->OnReceive(s);
    }).IgnoreError();

    return true;
}

cable::SocketPtr HalPlugToIPlugAdapter::OnUnplug() {
    // This is called on the QEMU thread when the guest disconnects.
    // We must fulfill the IPlug contract by returning the SocketPtr.
    //
    // First yank the socket, we do not want to start an immediate race
    // with the post call we make below that can also close the socket.
    //
    // Note: the marshalling socket can be a NullSocket if someone else was just
    // ahead of us when closing.
    auto marshalling_socket = std::static_pointer_cast<MarshallingHalSocket>(hal_plug_->Socket());
    VLOG(1) << "Closing and releasing " << *marshalling_socket;
    marshalling_socket->Close();
    auto released_socket = marshalling_socket->Release();

    // Now notify the client that we are no longer alive.
    VLOG(1) << "Scheduling onClose for mHalPlug:" << *hal_plug_;
    client_loop_->Post([plug = hal_plug_]() {
        VLOG(1) << "Calling OnClose from client thread on " << *plug;
        plug->OnClose();
    }).IgnoreError();

    return released_socket;
}

}  // namespace goldfish::devices
