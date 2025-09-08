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
#include "goldfish/hal/plug/HalPlugToIPlugAdapter.h"

#include <string_view>

#include "absl/log/log.h"

#include "goldfish/hal/plug/MarshallingHalSocket.h"

namespace goldfish {
namespace devices {

HalPlugToIPlugAdapter::~HalPlugToIPlugAdapter() {
    VLOG(1) << "Tearing down HalPlugToIPlugAdapter with mHalPlug: " << mHalPlug
            << ", use_count: " << mHalPlug.use_count();
}

HalPlugToIPlugAdapter::HalPlugToIPlugAdapter(async::EventLoop* clientLoop,
                                             std::shared_ptr<HalPlug> halPlug)
        : mClientLoop(clientLoop), mHalPlug(std::move(halPlug)) {
    VLOG(1) << "HalPlugToIPlugAdapter created with mHalPlug: " << mHalPlug
            << ", use_count: " << mHalPlug.use_count();
}

void HalPlugToIPlugAdapter::onConnect() {
    // Let's inform the client of the new connection.
    VLOG(1) << "Scheduling onConnect for mHalPlug";
    mClientLoop->post([plug = mHalPlug]() { plug->onConnect(); });
}

bool HalPlugToIPlugAdapter::onReceive(const void* data, size_t size) {
    // This is called on the QEMU thread.
    // We post the data to the client loop for the real HalPlug to handle.
    // We return true immediately, preventing the QEMU thread from blocking.
    //
    // This means that vsock will never close out this socket from this call.
    VLOG(1) << "Scheduling onReceive for mHalPlug with: " << std::string_view((char*)data, size);
    mClientLoop->post([plug = mHalPlug, s = std::string(static_cast<const char*>(data), size)]() {
        plug->onReceive(s);
    });

    return true;
}

cable::SocketPtr HalPlugToIPlugAdapter::onUnplug() {
    // This is called on the QEMU thread when the guest disconnects.
    // We must fulfill the IPlug contract by returning the SocketPtr.
    //
    // First yank the socket, we do not want to start an immediate race
    // with the post call we make below that can also close the socket.
    //
    // Note: the marshalling socket can be a NullSocket if someone else was just
    // ahead of us when closing.
    auto marshallingSocket = std::static_pointer_cast<MarshallingHalSocket>(mHalPlug->socket());
    marshallingSocket->close();
    auto releasedSocket = marshallingSocket->release();

    // Now notify the client that we are no longer alive.
    VLOG(1) << "Scheduling onClose for mHalPlug:" << mHalPlug;
    (void)mClientLoop->post([plug = mHalPlug]() {
        VLOG(1) << "Calling onClose from client thread on" << plug;
        plug->onClose();
    });

    return releasedSocket;
}

}  // namespace devices
}  // namespace goldfish
