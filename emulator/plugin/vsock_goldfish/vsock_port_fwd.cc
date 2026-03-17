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
#include "goldfish/vsock/vsock_port_fwd.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/avd_info/avd_private.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connection_awaiter.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/vsock/connect.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
}

// Remove windows compatibility defines.
#ifdef _WIN32
#undef send
#undef connect
#undef close
#undef socket
#endif

// IWYU pragma: end_keep
// clang-format on

// Vlog debug level.
#define VLOG_DBG 1
#define VLOG_TRACE 2

namespace {

using goldfish::async::AsyncSocketFactory;
using goldfish::async::EventLoop;
using goldfish::async::LibuvAsyncSocketFactory;
using goldfish::async::LibuvEventLoop;
using goldfish::async::QemuEventLoop;
using goldfish::async::ThreadedEventLoop;
using goldfish::avd_universe::guest_status::ObservableTimestamp;
using goldfish::devices::ConnectionAwaiter;
using goldfish::devices::HalPlugFactory;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::SocketPtr;
using goldfish::network::Endpoint;
using goldfish::network::EndpointFormatter;

// Sorts endpoints to prefer IPv4 over IPv6.
static bool isIpv4(const Endpoint& a) {
    return std::holds_alternative<goldfish::network::Ipv4Endpoint>(a);
}

/// @note all calls are on the clientEventloop, nothing is on the qemu event
/// loop.
class HostToGuestConnection : public goldfish::devices::HalPlug,
                              public std::enable_shared_from_this<HostToGuestConnection> {
  public:
    explicit HostToGuestConnection(std::shared_ptr<goldfish::async::AsyncSocket> hostSocket)
            : mHostSocket(std::move(hostSocket)) {
        assert(mHostSocket->GetLoop()->IsOnLoopThread() &&
               "The constructor should run on the event loop of the sockets.");
    }

    ~HostToGuestConnection() override { VLOG(1) << "Completed HostToGuestConnection."; }

    static std::shared_ptr<HostToGuestConnection> create(
            std::shared_ptr<goldfish::async::AsyncSocket> hostSocket) {
        auto connection = std::make_shared<HostToGuestConnection>(std::move(hostSocket));
        connection->mSelf = connection->shared_from_this();
        connection->mHostSocket->SetOnReadCallbackNoFlowControl(
                [pThis = connection.get()](std::string_view data, absl::Status status) {
                    pThis->onSocketReadCallback(data, status);
                });
        connection->mHostSocket->SetOnCloseCallback(
                [pThis = connection.get()]() { pThis->onSocketCloseCallback(); });
        return connection;
    }

    void onSocketReadCallback(std::string_view data, absl::Status status) {
        if (!status.ok()) {
            LOG(WARNING) << "Host (" << *mHostSocket << ") read failure, due to: " << status;
            Socket()->Close();
            return;
        }

        if (mGuestConnected) {
            VLOG(VLOG_TRACE) << "Host (" << *mHostSocket << ") forwarding: (" << data.size() << ") "
                             << data;
            Socket()->Send(std::string(data));
        } else {
            VLOG(VLOG_TRACE) << "Host (" << *mHostSocket << ") storing: (" << data.size() << ") "
                             << data;
            mHostBuffer.append(data);
        }
    }

    void onSocketCloseCallback() {
        VLOG(1) << "Host (" << *mHostSocket
                << ") closed, closing vsock, ref: " << mSelf.use_count();
        Socket()->Close();

        // Okay, we are ready to be deleted.
        mSelf.reset();
    }

    void OnConnect() override {
        VLOG(1) << "Guest (vsock) connected";
        mGuestConnected = true;
        if (!mHostBuffer.empty()) {
            /// @note we are on the client loop, so no-one is touching mHostBuffer.
            VLOG(1) << "Guest (vsock) receiving initial data: (" << mHostBuffer.size()
                    << ") :" << mHostBuffer;
            Socket()->Send(std::move(mHostBuffer));
            assert(mHostBuffer.empty());
        }
    }

    void OnReceive(std::string_view data) override {
        VLOG(VLOG_TRACE) << "Guest (vsock) forwarding: " << data << " to: " << *mHostSocket;
        mHostSocket->Send(data.data(), data.size()).IgnoreError();
    }

    void OnClose() override {
        VLOG(1) << "Guest (vsock) closed, closing: " << *mHostSocket;
        mHostSocket->Close();
    }

    const std::shared_ptr<goldfish::async::AsyncSocket> getHostSocket() const {
        return mHostSocket;
    }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[HostToGuestConnection guest:%v, host:%v]", *Socket(), *mHostSocket);
    }

  private:
    std::shared_ptr<goldfish::async::AsyncSocket> mHostSocket;
    std::shared_ptr<HostToGuestConnection> mSelf;
    std::string mHostBuffer;
    bool mGuestConnected{false};
};

/**
 * @brief This class implements a proxy that forwards traffic between a TCP port
 * on the host and a vsock port on the guest.
 *
 * Note that a connection to the guest will not necessarily succeed if the TCP
 * port in the guest is not yet up. We expect those who connect to the port to
 * be able to deal with the potential delays this can cause.
 */
class VSockProxyImpl : public VSockProxy {
  public:
    VSockProxyImpl(const Endpoint& hostEndpoint, VSockFwdDev* device,
                   const ObservableTimestamp& bootcompleteTime)
            : mHostEndpoint(hostEndpoint)
            , mDevice(device)
            , mQemuLoop(goldfish::avd_info::getQemuEventLoop())
            , mClientLoop(goldfish::async::globalEventLoop())
            , mBootcompleteTime(bootcompleteTime) {
        using namespace std::chrono_literals;
        mConnectionAwaiter = ConnectionAwaiter::RetryUntilConnected(
                mQemuLoop,
                [&](auto plug) {
                    if (isBootCompleted()) {
                        return goldfish::vsock::Connect(mDevice->guest_port, plug);
                    } else {
                        return SocketPtr{};
                    }
                    return SocketPtr{};
                },
                [&](SocketPtr sock) { vsockAliveOnQemuThread(); }, 100ms);
    }

    void close() {
        mClientLoop
                ->PostAndWait([this] {
                    if (mSocketServer) {
                        mSocketServer->Close();
                    }
                })
                .IgnoreError();
    }

  private:
    void startServer() {
        VLOG(1) << "Starting server on " << ToString(mHostEndpoint);
        auto incoming_socket_connection =
                [this](std::shared_ptr<goldfish::async::AsyncSocket> hostSocket) {
                    auto hostToGuest = HostToGuestConnection::create(std::move(hostSocket));
                    mQemuLoop
                            ->Post([this, hostToGuest = std::move(hostToGuest)] {
                                incomingConnectionOnQemuThread(std::move(hostToGuest));
                            })
                            .IgnoreError();
                    return true;
                };

        VLOG(1) << "Trying to bind to " << ToString(mHostEndpoint);
        mSocketServer =
                mSocketFactory.CreateServer(mClientLoop, mHostEndpoint, incoming_socket_connection);
        if (mSocketServer) {
            VLOG(1) << "Successfully bound to " << ToString(mHostEndpoint);
        }

        if (!mSocketServer) {
            LOG(FATAL) << "The VSockProxy that forwards the guest port: " << mDevice->guest_port
                       << " to the host: " << ToString(mHostEndpoint)
                       << " could not be created, error code: " << errno;
        }

        // Notify the world that we are available
        if (mDevice->on_connect) {
            mDevice->on_connect(mDevice);
        }
    }

    void vsockAliveOnQemuThread() {
        mClientLoop->Post([this] { startServer(); }).IgnoreError();
    }

    bool incomingConnectionOnQemuThread(std::shared_ptr<HostToGuestConnection> hostToGuest) {
        auto weakHostSocket =
                std::weak_ptr<goldfish::async::AsyncSocket>(hostToGuest->getHostSocket());
        auto onFlowControlEvent = [weakHostSocket = std::move(weakHostSocket)](bool enableReading) {
            if (const auto hostSocket = weakHostSocket.lock()) {
                hostSocket->OnFlowControlEvent(enableReading);
            }
        };

        VLOG(1) << "Received an incoming connection socket connection!";
        auto adapter = HalPlugFactory::Connect(
                mDevice->guest_port, [hostToGuest = std::move(hostToGuest)] { return hostToGuest; },
                mClientLoop, mQemuLoop, std::move(onFlowControlEvent),
                mDevice->data_sniffer_factory);
        VLOG(1) << "Adapter registered: " << adapter;
        return true;
    }

    bool isBootCompleted() const { return mBootcompleteTime.GetValue() != absl::UnixEpoch(); }

    /// The vsock device definition
    const Endpoint mHostEndpoint;
    VSockFwdDev* mDevice;
    EventLoop* mQemuLoop;    // The main QEMU event loop
    EventLoop* mClientLoop;  // Client-side event loop for sockets
    const ObservableTimestamp& mBootcompleteTime;
    LibuvAsyncSocketFactory mSocketFactory;

    /// The AsyncSocketServer used to listen for incoming connections.
    std::shared_ptr<goldfish::async::AsyncSocketServer> mSocketServer;

    /// Waiter that waits until the guest is connected.
    std::shared_ptr<ConnectionAwaiter> mConnectionAwaiter;
};
}  // namespace

// QEMU device configuration logic

static void vsock_fwd_realize(DeviceState* dev, Error** errp) {
    add_deletable_object(OBJECT(dev));
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);
    const char* host = vsock_fwd_device->address ? vsock_fwd_device->address : "localhost";
    auto serverAddress = absl::StrFormat("%s:%d", host, vsock_fwd_device->host_port);

    auto status = goldfish::network::ResolveEndpoints(serverAddress);
    if (!status.ok() || status.value().empty()) {
        error_setg(errp, "Could not resolve address: %s",
                   std::string(status.status().message()).c_str());
        return;
    }

    auto endpoints = status.value();
    auto preferred = std::find_if(endpoints.begin(), endpoints.end(), isIpv4);

    if (preferred == endpoints.end()) {
        preferred = endpoints.begin();
        LOG(WARNING) << "The address for the vsock port forwarder: " << serverAddress
                     << " does not resolve to an ipv4 address: "
                     << absl::StrJoin(endpoints, ", ", EndpointFormatter())
                     << " we will use: " << ToString(*preferred);
    }

    vsock_fwd_device->forwarder =
            new VSockProxyImpl(*preferred, vsock_fwd_device,
                               goldfish::avd_info::GetAvd().GetGuestStatus().bootcomplete);
    VLOG(VLOG_DBG) << "Realizing vsock forwarder: (address:host <-> guest) " << ToString(*preferred)
                   << "<->" << vsock_fwd_device->guest_port;
}

static void vsock_fwd_unrealize(DeviceState* dev) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);

    VLOG(VLOG_DBG) << "Erasing vsock forwarder: (host:guest) " << vsock_fwd_device->host_port << ":"
                   << vsock_fwd_device->guest_port;
    static_cast<VSockProxyImpl*>(vsock_fwd_device->forwarder)->close();
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
    if (value > 65535) {
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
    if (value > 65535) {
        error_setg(errp, "Port number should be between 0 and 65535, not: %d", value);
        return;
    }

    vsock_fwd_device->guest_port = value;
}

static void vsock_fwd_set_address(Object* obj, const char* value, Error** errp) {
    // Let's resolve this address..
    auto status = goldfish::network::ResolveEndpoints(value);
    if (!status.ok() || status.value().empty()) {
        error_setg(errp, "Could not resolve address: %s",
                   std::string(status.status().message()).c_str());
        return;
    }
    auto endpoints = status.value();
    auto preferred = std::find_if(endpoints.begin(), endpoints.end(), isIpv4);

    if (preferred == endpoints.end()) {
        preferred = endpoints.begin();
        LOG(WARNING) << "The address for the vsock port forwarder: " << value
                     << " does not resolve to an ipv4 address: "
                     << absl::StrJoin(endpoints, ", ", EndpointFormatter())
                     << " we will use: " << ToString(*preferred);
    }
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(obj);

    g_free(vsock_fwd_device->address);
    vsock_fwd_device->address = g_strdup(ToString(*preferred).c_str());
}

static void vsock_fwd_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "host_port", "int", NULL, vsock_fwd_set_host_port, NULL, NULL);
    object_class_property_set_description(oc, "host_port", "The host side port.");

    object_class_property_add(oc, "guest_port", "int", NULL, vsock_fwd_set_guest_port, NULL, NULL);
    object_class_property_set_description(
            oc, "guest_port",
            "The guest side port, incoming connections from the host will be "
            "forwarded to this port.");

    object_class_property_add_str(oc, "address", NULL, vsock_fwd_set_address);
    object_class_property_set_description(
            oc, "address", "The hostname to bind the portforwarder to, like localhost, or 0.0.0.0");

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

void vsock_port_fwd_register_types(void) {
    type_register_static(&vsock_fwd_type_info);
}
