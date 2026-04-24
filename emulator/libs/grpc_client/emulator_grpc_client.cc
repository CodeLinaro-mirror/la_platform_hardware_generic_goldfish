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
#include <thread>
#include <unordered_set>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "grpc_connection_monitor.h"

#include "android/emulation/control/basic_token_auth.h"
#include "android/emulation/control/grpc_channel_factory.h"
#include "android/goldfish/ini_file.h"
#include "android/status/status_macros.h"
#include "goldfish/eventing/event_sources.h"

namespace android::emulation::control {

namespace {
std::future<absl::Status> MakeReadyStatusFuture(absl::Status status) {
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
            : endpoint_(std::move(dest)), interceptors_(std::move(interceptors)) {}
    virtual ~EmulatorGrpcClientImpl() { Disconnect(); }

    const Endpoint& GetEndpoint() const { return endpoint_; }

    ConnectionState GetConnectionState() const { return state_.load(std::memory_order_acquire); }

    absl::Status Connect(absl::Duration timeout);
    std::future<absl::Status> ConnectAsync(absl::Duration timeout);
    void Disconnect();

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext();

    std::shared_ptr<::grpc::Channel> GetChannel() {
        const absl::MutexLock lock(channel_mutex_);
        return channel_;
    }

  private:
    void CreateChannelIfNeeded();
    absl::Status PrepareForConnection();

    Endpoint endpoint_;
    InterceptorFactories interceptors_;
    std::shared_ptr<grpc::CallCredentials> credentials_;
    std::atomic<ConnectionState> state_{ConnectionState::kDisconnected};

    absl::Mutex channel_mutex_;
    std::shared_ptr<::grpc::Channel> channel_ ABSL_GUARDED_BY(channel_mutex_);
    std::unique_ptr<GrpcConnectionMonitor> monitor_;
    GrpcConnectionMonitor::ScopedCallbackHandle monitor_callback_handle_;

    std::atomic<bool> shutting_down_{false};
    android::base::eventing::CallbackEventSource<ConnectionState> connection_state_source_;
};

absl::Status EmulatorGrpcClientImpl::PrepareForConnection() {
    ConnectionState expected = ConnectionState::kDisconnected;
    if (!state_.compare_exchange_strong(expected, ConnectionState::kConnecting)) {
        return absl::AlreadyExistsError("Connection is already active or connecting.");
    }
    shutting_down_.store(false, std::memory_order_release);
    connection_state_source_.FireEvent(ConnectionState::kConnecting);

    CreateChannelIfNeeded();
    if (!GetChannel()) {
        state_.store(ConnectionState::kDisconnected, std::memory_order_release);
        connection_state_source_.FireEvent(ConnectionState::kDisconnected);
        return absl::InvalidArgumentError(
                "Failed to create gRPC channel. Check TLS configuration for "
                "non-local addresses.");
    }
    return absl::OkStatus();
}

absl::Status EmulatorGrpcClientImpl::Connect(absl::Duration timeout) {
    RETURN_IF_ERROR(PrepareForConnection());

    const auto deadline = std::chrono::system_clock::now() + absl::ToChronoMilliseconds(timeout);
    if (GetChannel()->WaitForConnected(deadline)) {
        state_.store(ConnectionState::kConnected, std::memory_order_release);
        connection_state_source_.FireEvent(ConnectionState::kConnected);
        return absl::OkStatus();
    }

    state_.store(ConnectionState::kDisconnected, std::memory_order_release);
    connection_state_source_.FireEvent(ConnectionState::kDisconnected);
    return absl::DeadlineExceededError("Failed to connect within timeout.");
}

std::future<absl::Status> EmulatorGrpcClientImpl::ConnectAsync(absl::Duration timeout) {
    if (auto status = PrepareForConnection(); !status.ok()) {
        return MakeReadyStatusFuture(status);
    }

    monitor_ = std::make_unique<GrpcConnectionMonitor>(GetChannel());
    monitor_callback_handle_ = android::base::eventing::MakeScopedCallback(
            monitor_->state_changes(), [weak_self = weak_from_this()](ConnectionState state) {
                // Forward channel state event, (if we are still alive.)
                if (auto self = weak_self.lock()) {
                    self->state_.store(state, std::memory_order_release);
                    self->connection_state_source_.FireEvent(state);
                }
            });

    return monitor_->Watch(timeout);
}

void EmulatorGrpcClientImpl::Disconnect() {
    if (shutting_down_.exchange(true)) {
        return;  // Already shutting down.
    }
    monitor_callback_handle_ = {};
    {
        const absl::MutexLock lock(channel_mutex_);
        channel_.reset();
    }
    if (monitor_) {
        monitor_->Stop();
    }
    state_.store(ConnectionState::kDisconnected, std::memory_order_release);
    connection_state_source_.FireEvent(ConnectionState::kDisconnected);
}

absl::StatusOr<std::unique_ptr<grpc::ClientContext>> EmulatorGrpcClientImpl::NewContext() {
    if (GetConnectionState() != ConnectionState::kConnected) {
        return absl::FailedPreconditionError(
                "Client is not connected. Call Connect() or ConnectAsync() "
                "first.");
    }
    auto ctx = std::make_unique<grpc::ClientContext>();
    if (credentials_) {
        ctx->set_credentials(credentials_);
    }
    return ctx;
}

void EmulatorGrpcClientImpl::CreateChannelIfNeeded() {
    const absl::MutexLock lock(channel_mutex_);
    if (channel_) return;

    GrpcChannelFactory factory(endpoint_, std::move(interceptors_));
    channel_ = factory.CreateChannel();
    credentials_ = factory.Credentials();
}

// --- Base Class Implementation ---
EmulatorGrpcClientBase::EmulatorGrpcClientBase() : p_impl_(nullptr) {}
EmulatorGrpcClientBase::~EmulatorGrpcClientBase() = default;

const Endpoint& EmulatorGrpcClientBase::GetEndpoint() const {
    return p_impl_->GetEndpoint();
}

ConnectionState EmulatorGrpcClientBase::GetConnectionState() const {
    return p_impl_->GetConnectionState();
}

absl::StatusOr<std::unique_ptr<grpc::ClientContext>> EmulatorGrpcClientBase::NewContext() {
    return p_impl_->NewContext();
}

std::shared_ptr<::grpc::Channel> EmulatorGrpcClientBase::GetChannel() {
    return p_impl_->GetChannel();
}

// --- Blocking Client Implementation ---
absl::Status BlockingEmulatorGrpcClient::Connect(absl::Duration timeout) {
    return p_impl_->Connect(timeout);
}

void BlockingEmulatorGrpcClient::Disconnect() {
    p_impl_->Disconnect();
}

// --- Callback Client Implementation ---
std::future<absl::Status> CallbackEmulatorGrpcClient::ConnectAsync(absl::Duration timeout) {
    return p_impl_->ConnectAsync(timeout);
}

void CallbackEmulatorGrpcClient::Disconnect() {
    p_impl_->Disconnect();
}

android::base::eventing::CallbackEventSource<ConnectionState>&
CallbackEmulatorGrpcClient::ConnectionStateChanges() {
    return p_impl_->connection_state_source_;
}

// --- Builder Implementation ---
EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::WithDiscoveryFile(
        const std::filesystem::path& discovery_file) {
    if (!status_.ok()) return *this;
    destination_.Clear();
    android::goldfish::IniFile ini_file(discovery_file.string());
    ini_file.Read();
    if (!ini_file.HasKey("grpc.port")) {
        status_ = absl::InvalidArgumentError("No grpc port defined in " + discovery_file.string());
        return *this;
    }
    if (ini_file.HasKey("grpc.token")) {
        auto token = ini_file.GetString("grpc.token", "");
        auto* header = destination_.add_required_headers();
        header->set_key(android::emulation::control::BasicTokenAuth::kDefaultHeader);
        header->set_value("Bearer " + token);
    }
    destination_.set_target("localhost:" + ini_file.GetString("grpc.port", "8554"));
    return *this;
}

EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::ForDiscoveredEmulator(
        EmulatorProperties properties, const EmulatorAdvertisement& advertisement) {
    auto path = advertisement.DiscoverEmulatorWithProperties(properties);
    if (!path.ok()) {
        status_ = path.status();
        return *this;
    }
    return WithDiscoveryFile(*path);
}

EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::WithEndpoint(const Endpoint& endpoint) {
    if (!status_.ok()) return *this;
    destination_.Clear();
    destination_.CopyFrom(endpoint);
    return *this;
}

EmulatorGrpcClientBuilder& EmulatorGrpcClientBuilder::WithInterceptor(
        std::unique_ptr<ClientInterceptorFactoryInterface> factory) {
    if (!status_.ok()) return *this;
    factories_.push_back(std::move(factory));
    return *this;
}

absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>>
EmulatorGrpcClientBuilder::BuildBlocking() {
    if (!status_.ok()) {
        return status_;
    }
    auto client = std::make_unique<BlockingEmulatorGrpcClient>();
    client->p_impl_ = std::make_shared<EmulatorGrpcClientImpl>(destination_, std::move(factories_));
    return client;
}

absl::StatusOr<std::unique_ptr<CallbackEmulatorGrpcClient>>
EmulatorGrpcClientBuilder::BuildCallback() {
    if (!status_.ok()) {
        return status_;
    }
    auto client = std::make_unique<CallbackEmulatorGrpcClient>();
    client->p_impl_ = std::make_shared<EmulatorGrpcClientImpl>(destination_, std::move(factories_));
    return client;
}

}  // namespace android::emulation::control
