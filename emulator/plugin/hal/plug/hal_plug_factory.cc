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
#include "goldfish/devices/hal_plug_factory.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/marshalling_hal_socket.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"

namespace goldfish::devices {

PlugPtr HalPlugFactory::WrapHalPlug(SocketPtr qemu_socket, const HalDeviceFactory& hal_factory,
                                    EventLoop* client_loop, EventLoop* qemu_loop) {
    // 1. Create the user's HAL plug on the QEMU thread. This has to be a
    // synchronous call as we must give our vsockstream a concrete PlugPtr.
    // Let's hope developers are not doing
    // *crazy* things in the factory.
    std::shared_ptr<HalPlug> real_hal_plug = hal_factory();
    DCHECK(real_hal_plug) << "real_hal_plug is nullptr";

    // 2. Create the marshalling socket on the QEMU thread.
    auto marshalling_socket =
            std::make_shared<MarshallingHalSocket>(std::move(qemu_socket), qemu_loop);

    // 3. Set the socket on the HalPlug using the friend class.
    real_hal_plug->EstablishConnection(std::move(marshalling_socket));

    // 4. Post the onConnect notification to the client thread.
    VLOG(1) << "Scheduling on connect for real_hal_plug: " << *real_hal_plug
            << ", client_loop: " << client_loop;
    client_loop
            ->Post([real_hal_plug]() {
                VLOG(1) << "Delivering OnConnect to real_hal_plug: " << *real_hal_plug;
                real_hal_plug->OnConnect();
            })
            .IgnoreError();

    return std::make_shared<HalPlugToIPlugAdapter>(client_loop, std::move(real_hal_plug));
}

PlugPtr HalPlugFactory::Connect(int port, const HalDeviceFactory& hal_factory,
                                EventLoop* client_loop, EventLoop* qemu_loop,
                                cable::ISocket::OnFlowControlEvent on_flow_control_event,
                                const SnifferFactory& data_sniffer_factory) {
    const std::shared_ptr<HalPlug> real_hal_plug = hal_factory();
    auto plug = std::make_shared<HalPlugToIPlugAdapter>(client_loop, real_hal_plug);
    auto socket = vsock::Connect(port, plug);
    if (!socket) {
        return nullptr;
    }
    socket->SetOnFlowControlEvent(std::move(on_flow_control_event));
    if (data_sniffer_factory) {
        socket->SetDataSniffer(data_sniffer_factory());
    }

    auto marshalling_socket = std::make_shared<MarshallingHalSocket>(std::move(socket), qemu_loop);
    real_hal_plug->EstablishConnection(std::move(marshalling_socket));
    return plug;
}

bool HalPlugFactory::Listen(int port, HalDeviceFactory hal_factory, EventLoop* client_loop,
                            EventLoop* qemu_loop) {
    return vsock::Listen(port, [client_loop, qemu_loop,
                                hal_factory = std::move(hal_factory)](SocketPtr socket) mutable {
        return WrapHalPlug(std::move(socket), hal_factory, client_loop, qemu_loop);
    });
}

}  // namespace goldfish::devices
