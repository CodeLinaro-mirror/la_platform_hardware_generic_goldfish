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
#include "android/emulation/control/emulator_grpc_client.h"

#include <grpcpp/completion_queue.h>
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#include "aemu/base/events/EventSources.h"
#include "android/emulation/control/basic_token_auth.h"
#include "android/goldfish/ini_file.h"
#include "emulator/grpc/client/grpc_channel_factory.h"
#include "emulator/grpc/client/grpc_connection_monitor.h"

namespace android {
namespace emulation {
namespace control {

namespace {
static std::future<absl::Status> make_ready_status_future(absl::Status status) {
    std::promise<absl::Status> promise;
    promise.set_value(std::move(status));
    return promise.get_future();
}
}  // namespace

class EmulatorGrpcClientImpl : public std::enable_shared_from_this<EmulatorGrpcClientImpl> {
    friend class EmulatorGrpcClientBase;
    friend class CallbackEmulatorGrpcClient;

  public:
    explicit EmulatorGrpcClientImpl(Endpoint dest, InterceptorFactories interceptors)
            : mEndpoint(std::move(dest)), mInterceptors(std::move(interceptors)) {}
    virtual ~EmulatorGrpcClientImpl() { disconnect(); }

    const Endpoint& getEndpoint() const { return mEndpoint; }

    ConnectionState getConnectionState() const { return mState.load(std::memory_order_acquire); }

    absl::Status connect(absl::Duration timeout);
    std::future<absl::Status> connectAsync(absl::Duration timeout);
    void disconnect();

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> newContext();

    std::shared_ptr<::grpc::Channel> getChannel() {
        absl::MutexLock lock(&mChannelMutex);
        return mChannel;
    }

  private:
    void createChannelIfNeeded();

    Endpoint mEndpoint;
    InterceptorFactories mInterceptors;
    std::shared_ptr<grpc::CallCredentials> mCredentials;
    std::atomic<ConnectionState> mState{ConnectionState::Disconnected};

    absl::Mutex mChannelMutex;
    std::shared_ptr<::grpc::Channel> mChannel ABSL_GUARDED_BY(mChannelMutex);
    std::unique_ptr<GrpcConnectionMonitor> mMonitor;
    decltype(android::base::eventing::makeScopedCallback(
            std::declval<android::base::eventing::CallbackEventSource<ConnectionState>&>(),
            std::function<void(ConnectionState)>())) mMonitorCallbackHandle;

    std::atomic<bool> mShuttingDown{false};
    android::base::eventing::CallbackEventSource<ConnectionState> mConnectionStateSource;
};

absl::Status EmulatorGrpcClientImpl::connect(absl::Duration timeout) {
    ConnectionState expected = ConnectionState::Disconnected;
    if (!mState.compare_exchange_strong(expected, ConnectionState::Connecting)) {
        return absl::AlreadyExistsError("Connection is already active or connecting.");
    }
    mConnectionStateSource.fireEvent(ConnectionState::Connecting);

    createChannelIfNeeded();
    if (!getChannel()) {
        mState.store(ConnectionState::Disconnected, std::memory_order_release);
        mConnectionStateSource.fireEvent(ConnectionState::Disconnected);
        return absl::InvalidArgumentError(
                "Failed to create gRPC channel. Check TLS configuration for "
                "non-local addresses.");
    }

    auto deadline = std::chrono::system_clock::now() + absl::ToChronoMilliseconds(timeout);
    bool connected = getChannel()->WaitForConnected(deadline);

    if (connected) {
        mState.store(ConnectionState::Connected, std::memory_order_release);
        mConnectionStateSource.fireEvent(ConnectionState::Connected);
        return absl::OkStatus();
    } else {
        mState.store(ConnectionState::Disconnected, std::memory_order_release);
        mConnectionStateSource.fireEvent(ConnectionState::Disconnected);
        return absl::DeadlineExceededError("Failed to connect within timeout.");
    }
}

std::future<absl::Status> EmulatorGrpcClientImpl::connectAsync(absl::Duration timeout) {
    ConnectionState expected = ConnectionState::Disconnected;
    if (!mState.compare_exchange_strong(expected, ConnectionState::Connecting)) {
        return make_ready_status_future(
                absl::AlreadyExistsError("Connection is already active or connecting."));
    }
    mShuttingDown.store(false, std::memory_order_release);
    mConnectionStateSource.fireEvent(ConnectionState::Connecting);

    createChannelIfNeeded();
    if (!getChannel()) {
        mState.store(ConnectionState::Disconnected, std::memory_order_release);
        mConnectionStateSource.fireEvent(ConnectionState::Disconnected);
        return make_ready_status_future(absl::InternalError("Failed to create gRPC channel."));
    }

    mMonitor = std::make_unique<GrpcConnectionMonitor>(getChannel());
    mMonitorCallbackHandle = android::base::eventing::makeScopedCallback(
            mMonitor->mStateChanges, [weak_self = weak_from_this()](ConnectionState state) {
                // Forward channel state event, (if we are still alive.)
                if (auto self = weak_self.lock()) {
                    self->mState.store(state, std::memory_order_release);
                    self->mConnectionStateSource.fireEvent(state);
                }
            });

    return mMonitor->watch(timeout);
}

void EmulatorGrpcClientImpl::disconnect() {
    if (mShuttingDown.exchange(true)) {
        return;  // Already shutting down.
    }
    mMonitorCallbackHandle = {};
    {
        absl::MutexLock lock(&mChannelMutex);
        mChannel.reset();
    }
    if (mMonitor) {
        mMonitor->stop();
    }
    mState.store(ConnectionState::Disconnected, std::memory_order_release);
    mConnectionStateSource.fireEvent(ConnectionState::Disconnected);
}

absl::StatusOr<std::unique_ptr<grpc::ClientContext>> EmulatorGrpcClientImpl::newContext() {
    if (getConnectionState() != ConnectionState::Connected) {
        return absl::FailedPreconditionError(
                "Client is not connected. Call connect() or connectAsync() "
                "first.");
    }
    auto ctx = std::make_unique<grpc::ClientContext>();
    if (mCredentials) {
        ctx->set_credentials(mCredentials);
    }
    return ctx;
}

void EmulatorGrpcClientImpl::createChannelIfNeeded() {
    absl::MutexLock lock(&mChannelMutex);
    if (mChannel) return;

    GrpcChannelFactory factory(mEndpoint, std::move(mInterceptors));
    mChannel = factory.createChannel();
    mCredentials = factory.credentials();
}

// --- Base Class Implementation ---
EmulatorGrpcClientBase::EmulatorGrpcClientBase() : pImpl(nullptr) {}
EmulatorGrpcClientBase::~EmulatorGrpcClientBase() = default;

const Endpoint& EmulatorGrpcClientBase::getEndpoint() const {
    return pImpl->getEndpoint();
}

ConnectionState EmulatorGrpcClientBase::getConnectionState() const {
    return pImpl->getConnectionState();
}

absl::StatusOr<std::unique_ptr<grpc::ClientContext>> EmulatorGrpcClientBase::newContext() {
    return pImpl->newContext();
}

std::shared_ptr<::grpc::Channel> EmulatorGrpcClientBase::getChannel() {
    return pImpl->getChannel();
}

// --- Blocking Client Implementation ---
absl::Status BlockingEmulatorGrpcClient::connect(absl::Duration timeout) {
    return pImpl->connect(timeout);
}

void BlockingEmulatorGrpcClient::disconnect() {
    pImpl->disconnect();
}

// --- Callback Client Implementation ---
std::future<absl::Status> CallbackEmulatorGrpcClient::connectAsync(absl::Duration timeout) {
    return pImpl->connectAsync(timeout);
}

void CallbackEmulatorGrpcClient::disconnect() {
    pImpl->disconnect();
}

android::base::eventing::CallbackEventSource<ConnectionState>&
CallbackEmulatorGrpcClient::connectionStateChanges() {
    return pImpl->mConnectionStateSource;
}

// --- Builder Implementation ---
EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::withDiscoveryFile(
        const std::filesystem::path& discovery_file) {
    if (!mStatus.ok()) return *this;
    mDestination.Clear();
    android::goldfish::IniFile iniFile(discovery_file.string());
    iniFile.Read();
    if (!iniFile.HasKey("grpc.port")) {
        mStatus = absl::InvalidArgumentError("No grpc port defined in " + discovery_file.string());
        return *this;
    }
    if (iniFile.HasKey("grpc.token")) {
        auto token = iniFile.GetString("grpc.token", "");
        auto* header = mDestination.add_required_headers();
        header->set_key(android::emulation::control::BasicTokenAuth::DEFAULT_HEADER);
        header->set_value("Bearer " + token);
    }
    mDestination.set_target("localhost:" + iniFile.GetString("grpc.port", "8554"));
    return *this;
}

EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::withEndpoint(const Endpoint& endpoint) {
    if (!mStatus.ok()) return *this;
    mDestination.Clear();
    mDestination.CopyFrom(endpoint);
    return *this;
}

EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::withInterceptor(
        std::unique_ptr<ClientInterceptorFactoryInterface> factory) {
    if (!mStatus.ok()) return *this;
    mFactories.push_back(std::move(factory));
    return *this;
}

absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>>
EmulatorGrpcClientBuilder::buildBlocking() {
    if (!mStatus.ok()) {
        return mStatus;
    }
    auto client = std::make_unique<BlockingEmulatorGrpcClient>();
    client->pImpl = std::make_shared<EmulatorGrpcClientImpl>(mDestination, std::move(mFactories));
    return client;
}

absl::StatusOr<std::unique_ptr<CallbackEmulatorGrpcClient>>
EmulatorGrpcClientBuilder::buildCallback() {
    if (!mStatus.ok()) {
        return mStatus;
    }
    auto client = std::make_unique<CallbackEmulatorGrpcClient>();
    client->pImpl = std::make_shared<EmulatorGrpcClientImpl>(mDestination, std::move(mFactories));
    return client;
}

}  // namespace control
}  // namespace emulation
}  // namespace android
