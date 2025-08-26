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

#include "absl/log/log.h"

namespace goldfish {
namespace devices {

class NullHalSocket : public HalSocket {
  public:
    void send(std::string data) override {}
    void close() override {}
};

HalPlugToIPlugAdapter::~HalPlugToIPlugAdapter() {
    VLOG(1) << "Bye bye: mHalPlug: " << mHalPlug.use_count();
}

HalPlugToIPlugAdapter::HalPlugToIPlugAdapter(async::EventLoop* clientLoop,
                                             std::shared_ptr<HalPlug> halPlug)
        : mClientLoop(clientLoop), mHalPlug(std::move(halPlug)) {}

void HalPlugToIPlugAdapter::onConnect() {
    // Let's inform the client of the new connection.
    mClientLoop->post([plug = mHalPlug]() { plug->onConnect(); });
}

bool HalPlugToIPlugAdapter::onReceive(const void* data, size_t size) {
    // This is called on the QEMU thread.
    // We post the data to the client loop for the real HalPlug to handle.
    // We return true immediately, preventing the QEMU thread from blocking.
    //
    // This means that vsock will never close out this socket from this call.
    mClientLoop->post([plug = mHalPlug, s = std::string(static_cast<const char*>(data), size)]() {
        plug->onReceive(s);
    });

    return true;
}

cable::SocketPtr HalPlugToIPlugAdapter::onUnplug() {
    // This is called on the QEMU thread when the guest disconnects.
    // We post a task to notify the HalPlug on its own thread.
    VLOG(1) << "HalPlugToIPlugAdapter::onUnplug: Releasing socket.";
    mClientLoop->post([plug = mHalPlug]() { plug->onClose(); });
    return nullptr;
}

}  // namespace devices
}  // namespace goldfish
