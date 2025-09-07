// Copyright 2024 The Android Open Source Project
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
#include "goldfish/hal/plug/HalPlugFactory.h"

#include "absl/log/log.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/hal/plug/HalPlugToIPlugAdapter.h"
#include "goldfish/hal/plug/MarshallingHalSocket.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"

namespace goldfish {
namespace devices {

PlugPtr HalPlugFactory::wrapHalPlug(SocketPtr qemuSocket, HalDeviceFactory halFactory,
                                    EventLoop* clientLoop, EventLoop* qemuLoop) {
    // 1. Create the user's HAL plug on the QEMU thread. This has to be a
    // synchronous call as we must give our vsockstream a concrete PlugPtr.
    // Let's hope developers are not doing
    // *crazy* things in the factory.
    std::shared_ptr<HalPlug> realHalPlug = halFactory();

    // 2. Create the marshalling socket on the QEMU thread.
    auto marshallingSocket =
            std::make_shared<MarshallingHalSocket>(std::move(qemuSocket), qemuLoop);

    // 3. Set the socket on the HalPlug using the friend class.
    realHalPlug->establishConnection(std::move(marshallingSocket));

    // 4. Post the onConnect notification to the client thread.
    VLOG(1) << "Scheduling on connect for realHalPlug: " << *realHalPlug
            << ", clientLoop: " << clientLoop;
    clientLoop->post([realHalPlug]() {
        VLOG(1) << "Delivering onConnect to realHalPlug: " << realHalPlug;
        realHalPlug->onConnect();
    });

    return std::make_shared<HalPlugToIPlugAdapter>(clientLoop, std::move(realHalPlug));
}

PlugPtr HalPlugFactory::connect(int port, HalDeviceFactory halFactory, EventLoop* clientLoop,
                                EventLoop* qemuLoop, SnifferFactory dataSnifferFactory) {
    std::shared_ptr<HalPlug> realHalPlug = halFactory();
    auto plug = std::make_shared<HalPlugToIPlugAdapter>(clientLoop, realHalPlug);
    auto socket = vsock::connect(port, plug);
    if (!socket) {
        return nullptr;
    }
    if (dataSnifferFactory) {
        socket->setDataSniffer(dataSnifferFactory());
    }

    auto marshallingSocket = std::make_shared<MarshallingHalSocket>(std::move(socket), qemuLoop);
    realHalPlug->establishConnection(std::move(marshallingSocket));
    return plug;
}

bool HalPlugFactory::listen(int port, HalDeviceFactory halFactory, EventLoop* clientLoop,
                            EventLoop* qemuLoop) {
    return vsock::listen(port, [clientLoop, qemuLoop,
                                halFactory = std::move(halFactory)](SocketPtr socket) mutable {
        return wrapHalPlug(std::move(socket), std::move(halFactory), clientLoop, qemuLoop);
    });
}

}  // namespace devices
}  // namespace goldfish
