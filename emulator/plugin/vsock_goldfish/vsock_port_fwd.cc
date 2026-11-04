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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/avd/global_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connection_awaiter.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"
#include "goldfish/vsock/connect.h"
#include "vsock_port_fwd_internal.h"

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
using goldfish::avd_universe::guest_status::GuestStatus;
using goldfish::devices::ConnectionAwaiter;
using goldfish::devices::HalPlugFactory;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::SocketPtr;
using goldfish::network::Endpoint;
using goldfish::network::EndpointFormatter;

std::optional<std::vector<Endpoint>> ResolveForwarderEndpointsOrSetError(const std::string& address,
                                                                         Error** errp) {
    auto endpoints = goldfish::vsock::ResolveForwarderEndpoints(address);
    if (!endpoints.ok()) {
        error_setg(errp, "%s", std::string(endpoints.status().message()).c_str());
        return std::nullopt;
    }
    return *std::move(endpoints);
}

void SetPortProperty(Visitor* v, const char* name, int* port, Error** errp) {
    uint32_t value;
    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value > 65535) {
        error_setg(errp, "Invalid %s %u: port number must be between 0 and 65535", name, value);
        return;
    }
    *port = static_cast<int>(value);
}

/// @note all calls are on the clientEventloop, nothing is on the qemu event
/// loop.
class HostToGuestConnection : public goldfish::devices::HalPlug,
                              public std::enable_shared_from_this<HostToGuestConnection> {
  public:
    explicit HostToGuestConnection(std::shared_ptr<goldfish::async::AsyncSocket> hostSocket)
            : mHostSocket(std::move(hostSocket)) {
        DCHECK(mHostSocket->GetLoop()->IsOnLoopThread() &&
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
            Socket()->Send(data);
        } else {
            VLOG(VLOG_TRACE) << "Host (" << *mHostSocket << ") storing: (" << data.size() << ") "
                             << data;
            mHostBuffer.append(data);
        }
    }

    void onSocketCloseCallback() {
        VLOG(1) << "Host (" << *mHostSocket
                << ") closed, closing vsock, ref: " << mSelf.use_count();
        mHostClosed.store(true, std::memory_order_release);
        Socket()->Close();

        // Okay, we are ready to be deleted.
        mSelf.reset();
    }

    bool IsHostClosed() const { return mHostClosed.load(std::memory_order_acquire); }

    void OnConnect() override {
        if (mHostClosed.load(std::memory_order_acquire)) {
            Socket()->Close();
            return;
        }
        VLOG(1) << "Guest (vsock) connected";
        mGuestConnected = true;
        if (!mHostBuffer.empty()) {
            /// @note we are on the client loop, so no-one is touching mHostBuffer.
            VLOG(1) << "Guest (vsock) receiving initial data: (" << mHostBuffer.size()
                    << ") :" << mHostBuffer;
            Socket()->Send(mHostBuffer);
            mHostBuffer.clear();
        }
    }

    void OnReceive(std::string_view data) override {
        VLOG(VLOG_TRACE) << "Guest (vsock) forwarding: " << data << " to: " << *mHostSocket;
        if (!mHostSocket->Send(data.data(), data.size()).ok()) {
            Socket()->Close();
        }
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
    std::atomic<bool> mHostClosed{false};
    bool mGuestConnected{false};
};

class AdbPipePlug;

// Coordinates legacy `pipe:qemud:adb:<port>` guest pipes (API <= 30) with
// `VSockProxyImpl` host TCP listeners.
class AdbPipeRegistry {
  public:
    struct PendingHostConnection {
        std::shared_ptr<HostToGuestConnection> host_to_guest;
        EventLoop* client_loop = nullptr;
        EventLoop* qemu_loop = nullptr;
        goldfish::devices::cable::ISocket::OnFlowControlEvent on_flow_control;
        SnifferFactory sniffer_factory;
    };

    static AdbPipeRegistry& Get() {
        static absl::NoDestructor<AdbPipeRegistry> instance;
        return *instance;
    }

    void EnsureRegistered(goldfish::devices::IConnectorRegistry& registry);

    void AddWaitingPipe(int guest_port, const std::shared_ptr<AdbPipePlug>& plug);

    bool TryAcceptHostConnection(int guest_port, PendingHostConnection pending);

    bool HasWaitingPipe(int guest_port) const {
        absl::MutexLock lock(&mu_);
        const auto it = ports_.find(guest_port);
        if (it == ports_.end()) {
            return false;
        }
        for (const auto& weak_plug : it->second.waiting_plugs) {
            if (!weak_plug.expired()) {
                return true;
            }
        }
        return false;
    }

  private:
    struct PortState {
        std::deque<std::weak_ptr<AdbPipePlug>> waiting_plugs;
        std::deque<PendingHostConnection> pending_hosts;
        bool seen_adb_pipe = false;
    };

    mutable absl::Mutex mu_;
    absl::flat_hash_map<int, PortState> ports_ ABSL_GUARDED_BY(mu_);
};

// Implements the guest-side `pipe:qemud:adb:<port>` protocol ("accept" -> "ok" -> "start").
class AdbPipePlug final : public IPlug, public std::enable_shared_from_this<AdbPipePlug> {
  public:
    static constexpr std::string_view kSnapshotTypeId = "AdbPipePlug";
    static constexpr size_t kAcceptLen = 6;  // "accept"
    static constexpr size_t kStartLen = 5;   // "start"

    enum class State : uint8_t {
        kWaitingForAccept = 0,
        kWaitingForHostConnection = 1,
        kWaitingForStart = 2,
    };

    AdbPipePlug(SocketPtr socket, int guest_port, State state = State::kWaitingForAccept,
                std::string buf = {})
            : socket_(std::move(socket))
            , guest_port_(guest_port)
            , state_(state)
            , buf_(std::move(buf)) {}

    bool OnReceive(const void* data, size_t size) override {
        const auto self = shared_from_this();
        buf_.append(static_cast<const char*>(data), size);

        if (state_ == State::kWaitingForAccept) {
            if (buf_.size() < kAcceptLen) {
                return true;
            }
            if (std::memcmp(buf_.data(), "accept", kAcceptLen) != 0) {
                LOG(WARNING) << "AdbPipePlug: unexpected handshake on port " << guest_port_;
                return false;
            }
            buf_.erase(0, kAcceptLen);
            state_ = State::kWaitingForHostConnection;
            AdbPipeRegistry::Get().AddWaitingPipe(guest_port_, self);
            if (state_ != State::kWaitingForStart || buf_.empty()) {
                return true;
            }
        }

        if (state_ == State::kWaitingForStart) {
            if (buf_.size() < kStartLen) {
                return true;
            }
            if (std::memcmp(buf_.data(), "start", kStartLen) != 0) {
                LOG(WARNING) << "AdbPipePlug: unexpected start token on port " << guest_port_;
                return false;
            }
            const std::string trailing = buf_.substr(kStartLen);
            buf_.clear();

            socket_->SetOnFlowControlEvent(std::move(pending_.on_flow_control));
            if (pending_.sniffer_factory) {
                socket_->SetDataSniffer(pending_.sniffer_factory());
            }
            auto* raw_socket = socket_.get();
            auto host_to_guest = std::move(pending_.host_to_guest);
            EventLoop* client_loop = pending_.client_loop;
            EventLoop* qemu_loop = pending_.qemu_loop;
            pending_ = {};

            auto adapter = HalPlugFactory::WrapHalPlug(
                    std::move(socket_),
                    [host_to_guest = std::move(host_to_guest)] { return host_to_guest; },
                    client_loop, qemu_loop);
            auto old_plug = raw_socket->SwitchPlug(adapter);
            if (!trailing.empty()) {
                return adapter->OnReceive(trailing.data(), trailing.size());
            }
            return true;
        }

        return true;
    }

    void AcceptHostConnection(AdbPipeRegistry::PendingHostConnection pending) {
        pending_ = std::move(pending);
        state_ = State::kWaitingForStart;
        if (socket_) {
            socket_->SendAsync("ok", 2);
        }
    }

    SocketPtr OnUnplug() override {
        if (pending_.host_to_guest && pending_.client_loop) {
            pending_.client_loop
                    ->Post([host_to_guest = std::move(pending_.host_to_guest)] {
                        host_to_guest->OnClose();
                    })
                    .IgnoreError();
        }
        pending_ = {};
        return std::move(socket_);
    }

    bool SupportsLoadingFromSnapshot() const override {
        return state_ == State::kWaitingForAccept || state_ == State::kWaitingForHostConnection;
    }

    TypeId GetSnapshotTypeId() const override { return std::string(kSnapshotTypeId); }

    bool SaveStateToSnapshot(goldfish::archive::IWriter& writer) const override {
        if (!SupportsLoadingFromSnapshot()) {
            return false;
        }
        constexpr uint32_t kVersion = 1;
        writer << kVersion << guest_port_ << static_cast<uint8_t>(state_) << buf_.size();
        if (!buf_.empty()) {
            writer.Write(buf_.data(), buf_.size());
        }
        return true;
    }

    static goldfish::devices::cable::PlugOrSocket LoadFromSnapshot(
            SocketPtr socket, goldfish::archive::IReader& reader) {
        constexpr uint32_t kVersion = 1;
        uint32_t version = 0;
        int guest_port = 0;
        uint8_t raw_state = 0;
        size_t buf_size = 0;
        if (!goldfish::archive::ReadValue(reader, version, guest_port, raw_state, buf_size).ok() ||
            version != kVersion ||
            raw_state > static_cast<uint8_t>(State::kWaitingForHostConnection) ||
            buf_size > kAcceptLen) {
            return socket;
        }
        std::string buf(buf_size, '\0');
        if (buf_size > 0 && !reader.Read(buf.data(), buf_size).ok()) {
            return socket;
        }
        const auto state = static_cast<State>(raw_state);
        auto plug =
                std::make_shared<AdbPipePlug>(std::move(socket), guest_port, state, std::move(buf));
        if (state == State::kWaitingForHostConnection) {
            AdbPipeRegistry::Get().AddWaitingPipe(guest_port, plug);
        }
        return goldfish::devices::cable::PlugPtr(std::move(plug));
    }

  private:
    SocketPtr socket_;
    int guest_port_ = 5555;
    State state_ = State::kWaitingForAccept;
    std::string buf_;
    AdbPipeRegistry::PendingHostConnection pending_;
};

void AdbPipeRegistry::EnsureRegistered(goldfish::devices::IConnectorRegistry& registry) {
    static const bool kLoaderRegistered = goldfish::devices::cable::RegisterPlugLoader(
            std::string(AdbPipePlug::kSnapshotTypeId), &AdbPipePlug::LoadFromSnapshot);
    (void)kLoaderRegistered;
    registry.RegisterQemuDevice(
            "adb",
            [](SocketPtr socket, const std::shared_ptr<goldfish::devices::PingTopic>& /*ping*/,
               std::string_view args) -> goldfish::devices::cable::PlugPtr {
                int guest_port = 5555;
                if (!args.empty()) {
                    (void)absl::SimpleAtoi(args, &guest_port);
                }
                return std::make_shared<AdbPipePlug>(std::move(socket), guest_port);
            });
}

void AdbPipeRegistry::AddWaitingPipe(int guest_port, const std::shared_ptr<AdbPipePlug>& plug) {
    std::optional<PendingHostConnection> pending;
    {
        absl::MutexLock lock(&mu_);
        PortState& state = ports_[guest_port];
        state.seen_adb_pipe = true;
        while (!state.pending_hosts.empty()) {
            PendingHostConnection candidate = std::move(state.pending_hosts.front());
            state.pending_hosts.pop_front();
            if (candidate.host_to_guest != nullptr && !candidate.host_to_guest->IsHostClosed()) {
                pending = std::move(candidate);
                break;
            }
        }
        if (!pending.has_value()) {
            state.waiting_plugs.push_back(plug);
        }
    }
    if (pending.has_value()) {
        plug->AcceptHostConnection(std::move(*pending));
    }
}

bool AdbPipeRegistry::TryAcceptHostConnection(int guest_port, PendingHostConnection pending) {
    std::shared_ptr<AdbPipePlug> plug;
    {
        absl::MutexLock lock(&mu_);
        auto it = ports_.find(guest_port);
        if (it == ports_.end() || !it->second.seen_adb_pipe) {
            return false;
        }
        while (!it->second.waiting_plugs.empty()) {
            plug = it->second.waiting_plugs.front().lock();
            it->second.waiting_plugs.pop_front();
            if (plug != nullptr) {
                break;
            }
        }
        if (plug == nullptr) {
            it->second.pending_hosts.push_back(std::move(pending));
            return true;
        }
    }
    plug->AcceptHostConnection(std::move(pending));
    return true;
}

/**
 * @brief This class implements a proxy that forwards traffic between a TCP port
 * on the host and a vsock port on the guest.
 *
 * The host TCP port is bound on creation, and incoming connections are closed
 * immediately until the guest vsock port is reachable.
 */
class VSockProxyImpl {
    struct Private {};

  public:
    static absl::StatusOr<std::unique_ptr<VSockProxyImpl>> Create(
            std::vector<Endpoint> hostEndpoints, VSockFwdDev* device,
            goldfish::avd::AvdUniverse& universe) {
        AdbPipeRegistry::Get().EnsureRegistered(universe.GetConnectorRegistry());
        auto proxy = std::make_unique<VSockProxyImpl>(Private(), std::move(hostEndpoints), device,
                                                      universe);
        if (absl::Status status = proxy->startServer(); !status.ok()) {
            return status;
        }
        return proxy;
    }

    VSockProxyImpl(Private, std::vector<Endpoint> hostEndpoints, VSockFwdDev* device,
                   goldfish::avd::AvdUniverse& universe)
            : mHostEndpoints(std::move(hostEndpoints))
            , mDevice(device)
            , mQemuLoop(universe.GetQemuEventLoop())
            , mClientLoop(goldfish::avd::globalEventLoop())
            , mGuestStatus(universe.GetGuestStatus()) {}

    const Endpoint& boundEndpoint() const { return mBoundEndpoint; }

    void close() {
        mConnectionAwaiter.reset();
        mClientLoop
                ->PostAndWait([this] {
                    if (mSocketServer) {
                        mSocketServer->Close();
                    }
                })
                .IgnoreError();
        // Invalidate the lifetime token on the QEMU event loop after closing
        // the server on mClientLoop and before deleting this object. Any
        // connection tasks already posted from mClientLoop to mQemuLoop will
        // observe an expired weak_ptr when they later run on mQemuLoop.
        mAlive.reset();
    }

  private:
    absl::Status startServer() {
        absl::Status status = mClientLoop->PostAndWait([this]() -> absl::Status {
            VLOG(1) << "Starting server on ["
                    << absl::StrJoin(mHostEndpoints, ", ", EndpointFormatter()) << "]";
            auto incoming_socket_connection =
                    [this, alive = std::weak_ptr<void>(mAlive)](
                            std::shared_ptr<goldfish::async::AsyncSocket> hostSocket) {
                        if (!mGuestConnected.load(std::memory_order_acquire)) {
                            VLOG(1) << "Rejecting connection on " << ToString(mBoundEndpoint)
                                    << ": guest port " << mDevice->guest_port << " is not ready";
                            return false;
                        }
                        auto hostToGuest = HostToGuestConnection::create(std::move(hostSocket));
                        // Capture a weak lifetime token because this task is posted
                        // from mClientLoop and may execute on mQemuLoop after close()
                        // and deletion of VSockProxyImpl on mQemuLoop.
                        mQemuLoop
                                .Post([this, clientLoop = mClientLoop, alive,
                                       hostToGuest = std::move(hostToGuest)] {
                                    if (alive.lock()) {
                                        incomingConnectionOnQemuThread(std::move(hostToGuest));
                                    } else {
                                        // VSockProxyImpl was closed before this task ran;
                                        // close the socket on mClientLoop to release mSelf.
                                        clientLoop
                                                ->Post([hostToGuest = std::move(hostToGuest)] {
                                                    hostToGuest->OnClose();
                                                })
                                                .IgnoreError();
                                    }
                                })
                                .IgnoreError();
                        return true;
                    };

            auto bound_server = goldfish::vsock::BindForwarderServer(
                    mSocketFactory, mClientLoop, mHostEndpoints, mDevice->guest_port,
                    incoming_socket_connection);
            if (!bound_server.ok()) {
                return bound_server.status();
            }
            mSocketServer = std::move(bound_server->server);
            mBoundEndpoint = bound_server->endpoint;
            return absl::OkStatus();
        });
        if (!status.ok()) {
            return status;
        }

        using namespace std::chrono_literals;
        // Guard both ConnectionAwaiter callbacks with mAlive in case an in-flight
        // vsock stream keeps ConnectionAwaiter alive past mConnectionAwaiter.reset().
        mConnectionAwaiter = ConnectionAwaiter::RetryUntilConnected(
                &mQemuLoop,
                [this, alive = std::weak_ptr<void>(mAlive)](auto plug) {
                    if (!alive.lock()) {
                        return SocketPtr{};
                    }
                    if (AdbPipeRegistry::Get().HasWaitingPipe(mDevice->guest_port)) {
                        if (!mGuestConnected.load(std::memory_order_acquire)) {
                            vsockAliveOnQemuThread();
                            mQemuLoop
                                    .Post([this, alive] {
                                        if (alive.lock()) {
                                            mConnectionAwaiter.reset();
                                        }
                                    })
                                    .IgnoreError();
                        }
                        return SocketPtr{};
                    }
                    if (mGuestStatus.IsBootCompleted()) {
                        return goldfish::vsock::Connect(mDevice->guest_port, plug);
                    }
                    return SocketPtr{};
                },
                [this, alive = std::weak_ptr<void>(mAlive)](SocketPtr /*sock*/) {
                    if (alive.lock()) {
                        vsockAliveOnQemuThread();
                    }
                },
                100ms);
        return absl::OkStatus();
    }

    void vsockAliveOnQemuThread() {
        mGuestConnected.store(true, std::memory_order_release);
        if (mDevice->on_connect) {
            mClientLoop
                    ->Post([device = mDevice] {
                        // Notify the world that we are available
                        device->on_connect(device);
                    })
                    .IgnoreError();
        }
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
        if (AdbPipeRegistry::Get().TryAcceptHostConnection(
                    mDevice->guest_port, AdbPipeRegistry::PendingHostConnection{
                                             .host_to_guest = hostToGuest,
                                             .client_loop = mClientLoop,
                                             .qemu_loop = &mQemuLoop,
                                             .on_flow_control = onFlowControlEvent,
                                             .sniffer_factory = mDevice->data_sniffer_factory,
                                         })) {
            VLOG(1) << "Accepted incoming connection via legacy adb goldfish_pipe on port "
                    << mDevice->guest_port;
            return true;
        }
        auto adapter = HalPlugFactory::Connect(
                mDevice->guest_port, [hostToGuest = std::move(hostToGuest)] { return hostToGuest; },
                mClientLoop, &mQemuLoop, std::move(onFlowControlEvent),
                mDevice->data_sniffer_factory);
        VLOG(1) << "Adapter registered: " << adapter;
        return true;
    }

    /// The vsock device definition
    const std::vector<Endpoint> mHostEndpoints;
    Endpoint mBoundEndpoint;
    VSockFwdDev* mDevice;
    EventLoop& mQemuLoop;    // The main QEMU event loop
    EventLoop* mClientLoop;  // Client-side event loop for sockets
    const GuestStatus& mGuestStatus;
    LibuvAsyncSocketFactory mSocketFactory;
    std::atomic<bool> mGuestConnected{false};

    /// Lifetime token reset on mQemuLoop in close() to guard callbacks on
    /// mQemuLoop against use-after-free.
    std::shared_ptr<void> mAlive = std::make_shared<int>(0);

    /// The AsyncSocketServer used to listen for incoming connections.
    std::shared_ptr<goldfish::async::AsyncSocketServer> mSocketServer;

    /// Waiter that waits until the guest is connected.
    std::shared_ptr<ConnectionAwaiter> mConnectionAwaiter;
};
}  // namespace

// QEMU device configuration logic

static void vsock_fwd_realize(DeviceState* dev, Error** errp) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);
    const char* host = vsock_fwd_device->address ? vsock_fwd_device->address : "localhost";
    auto serverAddress = absl::StrFormat("%s:%d", host, vsock_fwd_device->host_port);

    auto endpoints = ResolveForwarderEndpointsOrSetError(serverAddress, errp);
    if (!endpoints) {
        return;
    }

    auto proxy = VSockProxyImpl::Create(*std::move(endpoints), vsock_fwd_device,
                                        goldfish::avd::GetAvd());
    if (!proxy.ok()) {
        error_setg(errp, "%s", std::string(proxy.status().message()).c_str());
        return;
    }
    const Endpoint boundEndpoint = (*proxy)->boundEndpoint();
    vsock_fwd_device->forwarder_impl = proxy->release();
    add_deletable_object(OBJECT(dev));
    VLOG(VLOG_DBG) << "Realizing vsock forwarder: (address:host <-> guest) "
                   << ToString(boundEndpoint) << "<->" << vsock_fwd_device->guest_port;
}

static void vsock_fwd_unrealize(DeviceState* dev) {
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(dev);

    VLOG(VLOG_DBG) << "Erasing vsock forwarder: (host:guest) " << vsock_fwd_device->host_port << ":"
                   << vsock_fwd_device->guest_port;

    VSockProxyImpl* impl = static_cast<VSockProxyImpl*>(vsock_fwd_device->forwarder_impl);
    if (impl) {
        impl->close();
        delete impl;
        vsock_fwd_device->forwarder_impl = nullptr;
    }
}

static void vsock_fwd_set_host_port(Object* obj, Visitor* v, const char* name, void* opaque,
                                    Error** errp) {
    SetPortProperty(v, name, &VSOCK_FWD_DEV(obj)->host_port, errp);
}

static void vsock_fwd_set_guest_port(Object* obj, Visitor* v, const char* name, void* opaque,
                                     Error** errp) {
    SetPortProperty(v, name, &VSOCK_FWD_DEV(obj)->guest_port, errp);
}

static void vsock_fwd_set_address(Object* obj, const char* value, Error** errp) {
    auto endpoints = ResolveForwarderEndpointsOrSetError(value, errp);
    if (!endpoints) {
        return;
    }
    VSockFwdDev* vsock_fwd_device = VSOCK_FWD_DEV(obj);

    g_free(vsock_fwd_device->address);
    vsock_fwd_device->address = g_strdup(value);
}

static void vsock_fwd_class_init(ObjectClass* oc, const void* data) {
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
