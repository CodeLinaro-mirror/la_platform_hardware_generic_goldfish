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
#include "goldfish/vsock/marshalling_plug.h"

#include "absl/log/log.h"

namespace goldfish {
namespace devices {
namespace cable {

MarshallingSocket::MarshallingSocket(SocketPtr socket, async::EventLoop* clientLoop,
                                     UnpluggerFn unplugger)
        : mClientLoop(clientLoop)
        , mQemuLoop(async::getQemuEventLoop())
        , mUnplugger(std::move(unplugger))
        , mWrappedSocket(std::move(socket)) {}

void MarshallingSocket::sendAsync(const void* data, size_t size) {
    VLOG(1) << "MarshallingSocket::sendAsync";
    (void)mQemuLoop->post([this, data = std::string(static_cast<const char*>(data), size)]() {
        if (mWrappedSocket) {
            VLOG(1) << "MarshallingSocket::sendAsync, on qemu thread:";
            mWrappedSocket->sendAsync(data.data(), data.size());
        }
    });
}

PlugPtr MarshallingSocket::switchPlug(PlugPtr newPlug) {
    VLOG(1) << "MarshallingSocket::switchPlug";
    return mWrappedSocket->switchPlug(std::move(newPlug));
}

void MarshallingSocket::setDataSniffer(std::unique_ptr<IDataSniffer> sniffer) {
    VLOG(1) << "MarshallingSocket::setDataSniffer";
    mWrappedSocket->setDataSniffer(std::move(sniffer));
}

PlugPtr MarshallingSocket::unplugImpl() {
    VLOG(1) << "MarshallingSocket::unplugImpl";
    return mUnplugger(mWrappedSocket.get());
}

MarshallingPlug::MarshallingPlug(async::EventLoop* clientLoop, PlugPtr plug)
        : mClientLoop(clientLoop)
        , mQemuLoop(async::getQemuEventLoop())
        , mClientPlug(std::move(plug)) {}

bool MarshallingPlug::onReceive(const void* data, size_t size) {
    VLOG(1) << "MarshallingPlug::onReceive to client";
    return mClientLoop->postAndWait([&] {
        VLOG(1) << "MarshallingPlug::onReceive, on client thread";
        return mClientPlug->onReceive(data, size);
    });
}

void MarshallingPlug::onConnect() {
    VLOG(1) << "MarshallingPlug::onConnect";
    mClientPlug->onConnect();
}

SocketPtr MarshallingPlug::onUnplug() {
    VLOG(1) << "MarshallingPlug::onUnplug";
    return mClientPlug->onUnplug();
}

bool MarshallingPlug::supportsLoadingFromSnapshot() const {
    VLOG(1) << "MarshallingPlug::supportsLoadingFromSnapshot";
    return mClientPlug->supportsLoadingFromSnapshot();
}

MarshallingPlug::TypeId MarshallingPlug::getSnapshotTypeId() const {
    VLOG(1) << "MarshallingPlug::getSnapshotTypeId";
    return mClientPlug->getSnapshotTypeId();
}

bool MarshallingPlug::saveStateToSnapshot(archive::IWriter& writer) const {
    VLOG(1) << "MarshallingPlug::saveStateToSnapshot";
    return mClientPlug->saveStateToSnapshot(writer);
}

bool listenWithMarshalling(uint32_t hostPort, vsock::HostPortListener listener,
                           async::EventLoop* clientLoop, UnpluggerFn unplugger) {
    // This wrapper lambda is passed to the underlying vsock::listen.
    // It's executed on the QEMU thread whenever a new connection arrives.
    auto connectionWrapper = [clientLoop, listener,
                              unplugger](devices::cable::SocketPtr qemuSocket) -> PlugOrSocket {
        // This task will be marshalled to the client's event loop.
        auto clientTask = [listener, clientLoop, unplugger,
                           qemuSock = std::move(qemuSocket)]() mutable -> PlugOrSocket {
            // On the client thread, wrap the QEMU socket with our marshaller.
            auto marshallingSocket =
                    new MarshallingSocket(std::move(qemuSock), clientLoop, unplugger);

            // Call the user's listener with the thread-safe marshalling socket.
            return listener(SocketPtr(marshallingSocket));
        };

        // Execute the task on the client loop and wait for the result.
        PlugOrSocket plugOrSocket = clientLoop->postAndWait(std::move(clientTask));

        // The listener returns a PlugPtr on success. We need to wrap this plug
        // in a MarshallingPlug so its methods are called back on the client thread.
        if (auto* plug = std::get_if<PlugPtr>(&plugOrSocket)) {
            return std::make_shared<MarshallingPlug>(clientLoop, std::move(*plug));
        }

        // If the listener returns a SocketPtr, it's a rejection.
        // We return the original socket to the caller on the QEMU thread.
        return plugOrSocket;
    };

    auto qemuLoop = async::getQemuEventLoop();

    // If we're already on the QEMU loop, we can call listen directly.
    // Otherwise, we must post the listen call to the QEMU loop and wait.
    if (qemuLoop->isOnLoopThread()) {
        return vsock::listen(hostPort, std::move(connectionWrapper));
    }
    return qemuLoop->postAndWait([hostPort, wrapper = std::move(connectionWrapper)]() mutable {
        return vsock::listen(hostPort, std::move(wrapper));
    });
}

devices::cable::SocketPtr connectWithMarshalling(uint32_t guestPort,
                                                 devices::cable::PlugPtr clientPlug,
                                                 async::EventLoop* clientLoop,
                                                 UnpluggerFn unplugger) {
    // This task will be executed on the QEMU event loop.
    auto connectTask = [=, clientPlug = std::move(clientPlug)]() mutable {
        // 1. On the QEMU thread, wrap the client's plug in a MarshallingPlug.
        //    This ensures that any calls from the QEMU-side socket back to
        //    the plug are safely marshalled to the client's event loop.
        auto marshallingPlug = std::make_shared<MarshallingPlug>(clientLoop, std::move(clientPlug));

        // 2. Establish the connection to the guest, providing the marshalling plug.
        //    This returns a raw socket that lives on the QEMU thread.
        devices::cable::SocketPtr qemuSocket =
                vsock::connect(guestPort, std::move(marshallingPlug));
        if (!qemuSocket) {
            return devices::cable::SocketPtr(nullptr);  // Connection failed.
        }

        // 3. Wrap the raw QEMU socket in a MarshallingSocket.
        //    This provides a thread-safe interface that the client can use to
        //    send data *to* the QEMU thread.
        return SocketPtr(new MarshallingSocket(std::move(qemuSocket), clientLoop, unplugger));
    };

    auto qemuLoop = async::getQemuEventLoop();

    // If we're already on the QEMU loop, execute the task directly.
    // Otherwise, post it to the QEMU loop and wait for the result.
    if (qemuLoop->isOnLoopThread()) {
        return connectTask();
    }
    return qemuLoop->postAndWait(std::move(connectTask));
}

}  // namespace cable
}  // namespace devices
}  // namespace goldfish