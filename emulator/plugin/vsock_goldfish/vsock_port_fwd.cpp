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
#include <memory>
#include <mutex>
#include <string_view>

#include "absl/log/log.h"

#include "aemu/base/async/AsyncSocket.h"
#include "aemu/base/async/AsyncSocketServer.h"
#include "aemu/base/sockets/ScopedSocket.h"
#include "android/goldfish/qemu-looper.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/vsock/connect.h"
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/atomic.hpp"

extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
}

// Remove windows compatibility defines.
#ifdef _WIN32
#undef send
#undef connect
#endif

// IWYU pragma: end_keep
// clang-format on

// Vlog debug level.
#define VLOG_DBG 1
#define VLOG_TRACE 2

using android::base::AsyncSocket;
using android::base::AsyncSocketAdapter;
using android::base::AsyncSocketEventListener;
using android::base::AsyncSocketServer;
using android::base::SimpleAsyncSocket;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::ISocket;
using goldfish::devices::cable::SocketPtr;

namespace {
class HostToGuestConnection : public IPlug {
  public:
    HostToGuestConnection(int fd, int guestPort)
        : mAsyncSocket(android::goldfish::qemuLooper(), android::base::ScopedSocket(fd)),
          mHostSocket(
                  &mAsyncSocket, [this](std::string_view bytes) { receiveHost(bytes); },
                  [this]() { closeHost(); }),
          mGuestPort(guestPort) {}

    ~HostToGuestConnection() {
        VLOG(VLOG_DBG) << "Connection to " << mGuestPort << " is finalized.";
    }

    void receiveHost(std::string_view bytes) {
        if (!mConnected) {
            std::lock_guard<std::mutex> lock(mConnectedMutex);
            if (!mConnected) {
                VLOG(VLOG_TRACE) << "Prepending from host: " << bytes.size();
                mPreConnectedBuffer.append(bytes);
                return;
            }
        }
        if (!mGuestSocket) {
            return;
        }
        static int total = 0;
        VLOG(VLOG_TRACE) << "Forwarding from host: " << bytes.size()
                         << " total: " << (total += bytes.size());
        mGuestSocket->sendAsync(bytes.data(), bytes.size());
    }

    void closeHost() {
        std::lock_guard<std::recursive_mutex> lock(mClosing);

        // Make sure we clean up any outstanding events.
        mHostSocket.dispose();
        if (!mDisposing) {
            mDisposing = true;
            VLOG(VLOG_DBG) << "The host is closing the connection, unplugging.";
            ISocket::unplug(std::move(mGuestSocket));
        } else {
            VLOG(VLOG_DBG) << "The guest is closing, and called close on the host.";
        }
    }

    static void connectToGuest(std::shared_ptr<HostToGuestConnection> connection) {
        VLOG(VLOG_DBG) << "Connecting to guest over vsock on port: " << connection->mGuestPort;
        connection->mGuestSocket = goldfish::vsock::connect(connection->mGuestPort, connection);
    }

    void onConnect() override {
        // Nothing needs to happen here.
        VLOG(VLOG_DBG) << "Connected to guest at port: " << mGuestPort;
        std::lock_guard<std::mutex> lock(mConnectedMutex);
        mConnected = true;

        if (mPreConnectedBuffer.empty()) {
            return;
        }
        VLOG(VLOG_TRACE) << "Sending bytes received before connection: "
                         << mPreConnectedBuffer.size();
        mGuestSocket->sendAsync(mPreConnectedBuffer.data(), mPreConnectedBuffer.size());
        mPreConnectedBuffer.clear();
    }

    bool onReceive(const void* data, size_t size) override {
        static int total = 0;
        VLOG(VLOG_TRACE) << "Forwarding from guest (" << mGuestPort << "): " << size
                         << ", total: " << (total += size);
        return mHostSocket.send((char*)data, size) == size;
    }

    SocketPtr onUnplug() override {
        std::lock_guard<std::recursive_mutex> lock(mClosing);
        VLOG(VLOG_DBG) << "The guest has unplugged, closing socket.";
        if (!mDisposing) {
            mDisposing = true;
            mHostSocket.dispose();
        }
        return std::move(mGuestSocket);
    }

  private:
    int mGuestPort;
    bool mDisposing{false};
    std::recursive_mutex mClosing;
    SocketPtr mGuestSocket;
    AsyncSocket mAsyncSocket;
    SimpleAsyncSocket mHostSocket;

    bool mConnected{false};
    std::mutex mConnectedMutex;
    std::string mPreConnectedBuffer;
};

/**
 * @brief This class implements a proxy that forwards traffic between a TCP port
 * on the host and a vsock port on the guest.
 *
 * Note that a connection to the guest will not necessarily succeed if the TCP port in
 * the guest is not yet up. We expect those who connect to the port to be able with
 * the potential delays this can cause.
 */
class VSockProxy {
  public:
    /**
     * @brief Constructs a VSockProxy object.
     *
     * @param guestPort The vsock port on the guest to forward to.
     * @param hostPort The TCP port on the host to listen on.
     */
    VSockProxy(int guestPort, int hostPort) : mGuestPort(guestPort), mHostPort(hostPort) {
        startServer();
    }

  private:
    void startServer() {
        mSocketServer = AsyncSocketServer::createTcpLoopbackServer(
                mHostPort, [this](int fd) { return acceptIncomingSocket(fd); },
                AsyncSocketServer::LoopbackMode::kIPv4AndIPv6, android::goldfish::qemuLooper());
        if (!mSocketServer) {
            LOG(FATAL) << "The VSockProxy that forwards the guest port: " << mGuestPort
                       << " to the host: " << mHostPort
                       << " could not be created, error code: " << errno;
        }
        mSocketServer->startListening();
    }

    /**
     * @brief Accepts an incoming connection from the host.
     *
     * @param fd The file descriptor of the accepted socket.
     * @return True if the socket was accepted successfully, false otherwise.
     */
    bool acceptIncomingSocket(int fd) {
        VLOG(VLOG_DBG) << "Accepting connection from: " << mHostPort << " with fd: " << fd;
        auto forward = std::make_shared<HostToGuestConnection>(fd, mGuestPort);
        HostToGuestConnection::connectToGuest(forward);
        mSocketServer->startListening();
        return true;
    }

    /// The vsock port on the guest to forward to.
    int mGuestPort;
    /// The TCP port on the host to listen on.
    int mHostPort;
    /// The AsyncSocketServer used to listen for incoming connections.
    std::unique_ptr<AsyncSocketServer> mSocketServer;
};
}  // namespace

// QEMU device configuration logic

struct VSockFwdDev {
    DeviceClass parent_class;
    int host_port;
    int guest_port;
    VSockProxy* forwarder;
};

#define TYPE_VSOCK_FWD "virtio-goldfish-hostfwd-socket"
#define VSOCK_FWD_DEV(obj) OBJECT_CHECK(VSockFwdDev, (obj), TYPE_VSOCK_FWD)
#define VSOCK_FWD_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(VSockFwdDev, obj, TYPE_VSOCK_FWD)

static void vsock_fwd_realize(DeviceState* dev, Error** errp) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);
    vsock_fwd_device->forwarder =
            new VSockProxy(vsock_fwd_device->guest_port, vsock_fwd_device->host_port);

    VLOG(VLOG_DBG) << "Realizing vsock forwarder: (host:guest) " << vsock_fwd_device->host_port
                   << ":" << vsock_fwd_device->guest_port;
}

static void vsock_fwd_unrealize(DeviceState* dev) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);

    VLOG(VLOG_DBG) << "Erasing vsock forwarder: (host:guest) " << vsock_fwd_device->host_port << ":"
                   << vsock_fwd_device->guest_port;
    delete vsock_fwd_device->forwarder;
}

static void vsock_fwd_set_host_port(Object* obj, Visitor* v, const char* name, void* opaque,
                                    Error** errp) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    // Check for invalid input or overflow
    if (value < 0 || value > 65535) {
        error_setg(errp, "Port number should be between 0 and 65535, not: %d", value);
        return;
    }

    vsock_fwd_device->host_port = value;
}

static void vsock_fwd_set_guest_port(Object* obj, Visitor* v, const char* name, void* opaque,
                                     Error** errp) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    // Check for invalid input or overflow
    if (value < 0 || value > 65535) {
        error_setg(errp, "Port number should be between 0 and 65535, not: %d", value);
        return;
    }

    vsock_fwd_device->guest_port = value;
}

static void vsock_fwd_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "host_port", "int", NULL, vsock_fwd_set_host_port, NULL, NULL);
    object_class_property_set_description(oc, "host_port", "The host side port.");

    object_class_property_add(oc, "guest_port", "int", NULL, vsock_fwd_set_guest_port, NULL, NULL);
    object_class_property_set_description(
            oc, "guest_port",
            "The guest side port, incoming connections from the host will be "
            "forwarded to this port.");

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = vsock_fwd_realize;
    dc->unrealize = vsock_fwd_unrealize;
}

static const TypeInfo vsock_fwd_type_info = {
        .name = TYPE_VSOCK_FWD,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(VSockFwdDev),
        .class_init = vsock_fwd_class_init,
};

static void register_types(void) {
    type_register_static(&vsock_fwd_type_info);
}
type_init(register_types);
