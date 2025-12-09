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
#pragma once

#include <grpcpp/grpcpp.h>

#include <filesystem>
#include <future>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpc_endpoint_description.pb.h"

#include "aemu/base/events/EventSources.h"

namespace android {
namespace emulation {
namespace control {

// Forward declarations
class BlockingEmulatorGrpcClient;
class CallbackEmulatorGrpcClient;
class EmulatorGrpcClientBuilder;

using ::android::emulation::remote::Endpoint;
using grpc::experimental::ClientInterceptorFactoryInterface;
using InterceptorFactory = std::unique_ptr<ClientInterceptorFactoryInterface>;
using InterceptorFactories = std::vector<InterceptorFactory>;

/**
 * @brief Represents the current state of the gRPC connection.
 */
enum class ConnectionState {
    Disconnected,  ///< The client is not connected.
    Connecting,    ///< A connection attempt is in progress.
    Connected      ///< The client is connected and ready for RPC calls.
};

// PIMPL class, defined in the .cpp file.
class EmulatorGrpcClientImpl;

/**
 * @brief A non-public base class for gRPC clients.
 *
 * This class owns the implementation (PIMPL) and provides the common,
 * non-virtual API shared by both the blocking and callback-based clients.
 */
class EmulatorGrpcClientBase {
  public:
    virtual ~EmulatorGrpcClientBase();

    /**
     * @brief Gets the endpoint configuration for this client.
     * @return A const reference to the `Endpoint` object.
     */
    const Endpoint& getEndpoint() const;

    /**
     * @brief Gets the current connection state.
     * @return The current `ConnectionState`.
     */
    ConnectionState getConnectionState() const;

    /**
     * @brief Creates a new gRPC stub for a specific service.
     *
     * This template method provides a convenient way to get a typed stub for
     * making RPC calls. The client must be in the `Connected` state before
     * calling this method.
     *
     * @tparam T The gRPC service type (e.g., `EmulatorController::Service`).
     * @return A `StatusOr` containing either a unique pointer to the stub or an
     *         error if the client is not connected.
     */
    template <class T>
    absl::StatusOr<std::unique_ptr<typename T::Stub>> stub() {
        if (getConnectionState() != ConnectionState::Connected) {
            return absl::FailedPreconditionError(
                    "Client is not connected. Call connect() or connectAsync() "
                    "first.");
        }
        return T::NewStub(getChannel());
    }

    /**
     * @brief Creates a new `ClientContext` for an RPC call.
     *
     * This method pre-configures the context with any necessary security
     * details (like authentication tokens) derived from the `Endpoint`
     * configuration.
     *
     * @return A `StatusOr` containing either the context or an error.
     */
    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> newContext();

  protected:
    EmulatorGrpcClientBase();
    friend class EmulatorGrpcClientBuilder;

    std::shared_ptr<::grpc::Channel> getChannel();
    std::shared_ptr<EmulatorGrpcClientImpl> pImpl;
};

/**
 * @brief A simple, synchronous gRPC client for the emulator.
 *
 * This client provides a straightforward, blocking API for connecting and
 * making RPC calls. It is ideal for simple, short-lived tasks or scripts.
 *
 * @par Trade-offs
 * This client is lightweight and creates no background threads. However, it
 * does not perform automatic liveness monitoring. If the connection is dropped
 * by the server, this client will not be aware of it until the next RPC call
 * fails.
 *
 * @par Example
 * @code
 *   // Note: In a real application, you would need to include the specific
 *   // service headers (e.g., "emulator_controller.grpc.pb.h").
 *
 *   auto builder = EmulatorGrpcClientBuilder()
 *                      .withDiscoveryFile("path/to/discovery.ini");
 *
 *   auto clientOrStatus = builder.buildBlocking();
 *   if (!clientOrStatus.ok()) { // handle config error/ }
 *   auto client = std::move(*clientOrStatus);
 *
 *   absl::Status status = client->connect(absl::Seconds(5));
 *   if (!status.ok()) { // handle connection error  }
 *
 *   // ... make RPC calls ...
 *
 *   client->disconnect();
 * @endcode
 */
class BlockingEmulatorGrpcClient : public EmulatorGrpcClientBase {
  public:
    /**
     * @brief Establishes a synchronous connection to the gRPC server.
     *
     * This method blocks until the connection is established or the timeout
     * expires.
     *
     * @param timeout The maximum time to wait for a connection.
     * @return `absl::OkStatus()` on success, or an error status on failure.
     */
    absl::Status connect(absl::Duration timeout);

    /**
     * @brief Closes the gRPC connection.
     */
    void disconnect();
};

/**
 * @brief An advanced, asynchronous gRPC client for the emulator.
 *
 * This client provides a non-blocking, callback-based API for connecting and
 * making RPC calls. It is designed for long-lived applications, such as user
 * interfaces, that need to remain responsive and aware of the connection status.
 *
 * @par Trade-offs
 * This client provides significant benefits, including a non-blocking `connectAsync`
 * method, a `connectionStateChanges` event source for reactive UI updates, and
 * automatic liveness monitoring to detect dropped connections. This is achieved
 * by managing a background worker thread, which introduces a small amount of
 * resource overhead compared to the `BlockingEmulatorGrpcClient`.
 *
 * @par Example
 * @code
 *   auto builder = EmulatorGrpcClientBuilder()
 *                      .withEndpoint(endpoint);
 *
 *   auto clientOrStatus = builder.buildCallback();
 *   if (!clientOrStatus.ok()) { // handle config error  }
 *   auto client = std::move(*clientOrStatus);
 *
 *   // Subscribe to state changes to update a UI element.
 *   auto handle = android::base::eventing::makeScopedCallback(
 *       client->connectionStateChanges(),
 *       [](ConnectionState state) {
 *           // Update UI based on state: Connecting, Connected, Disconnected
 *       });
 *
 *   // Initiate a non-blocking connection.
 *   std::future<absl::Status> future = client->connectAsync(absl::Seconds(10));
 *   future.wait(); // Or handle the result on another thread.
 *
 *   // ... application logic ...
 * @endcode
 */
class CallbackEmulatorGrpcClient : public EmulatorGrpcClientBase {
  public:
    /**
     * @brief Initiates a non-blocking connection attempt.
     *
     * This method returns immediately and performs the connection attempt in the
     * background.
     *
     * @param timeout The maximum time to wait for a connection.
     * @return A `std::future` that will be fulfilled with the connection status
     *         (`absl::OkStatus()` on success).
     */
    std::future<absl::Status> connectAsync(absl::Duration timeout);

    /**
     * @brief Closes the gRPC connection and stops the monitoring thread.
     */
    void disconnect();

    /**
     * @brief Gets an event source for monitoring connection state changes.
     *
     * Applications can subscribe to this event source to receive notifications
     * when the client connects, disconnects, or starts a connection attempt.
     *
     * @return A reference to the `CallbackEventSource`.
     */
    android::base::eventing::CallbackEventSource<ConnectionState>& connectionStateChanges();
};

/**
 * @brief A builder for constructing all types of EmulatorGrpcClient instances.
 *
 * This factory is the single entry point for creating either a simple
 * `BlockingEmulatorGrpcClient` or an advanced `CallbackEmulatorGrpcClient`.
 */
class EmulatorGrpcClientBuilder {
  public:
    /**
     * @brief Configures the client using an emulator discovery file.
     *
     * The discovery file is a properties file (key-value pairs) that the
     * emulator writes at startup. It provides a flexible way to configure the
     * gRPC connection details without hardcoding them.
     *
     * The builder parses this file to determine the server address and security
     * settings. The `grpc.port` property is used to construct the target
     * address (e.g., "localhost:<port>"). If a `grpc.token` is present, it
     * will be automatically added as a `Bearer` token to the `authorization`
     * header for every RPC call.
     *
     * @param discovery_file The path to the discovery file.
     * @return A reference to the builder for chaining.
     *
     * @par Example Discovery File
     * @code
     *   # discovery.properties
     *   grpc.port=8554
     *   grpc.token=abcdef12345
     *   ...
     * @endcode
     */
    EmulatorGrpcClientBuilder& withDiscoveryFile(const std::filesystem::path& discovery_file);

    /**
     * @brief Configures the client using a pre-constructed `Endpoint` object.
     *
     * This method allows for programmatic configuration of the gRPC connection.
     * The `Endpoint` message defines the server's address, security credentials,
     * and any custom metadata headers that should be sent with every RPC call.
     *
     * @param endpoint The `Endpoint` object with the desired configuration.
     * @return A reference to the builder for chaining.
     *
     * @par Example: Using a JWT Token
     * @code
     *   Endpoint endpoint;
     *   endpoint.set_target("localhost:8554");
     *
     *   // Add a JWT token for authentication.
     *   auto* security = endpoint.mutable_security();
     *   auto& headers = *security->mutable_metadata_headers();
     *   headers["authorization"] = "Bearer your-jwt-token-here";
     *
     *   // You can also configure TLS from a file.
     *   // security->set_server_ca_certificate("path/to/ca.pem");
     *
     *   auto client = EmulatorGrpcClientBuilder()
     *                     .withEndpoint(endpoint)
     *                     .buildBlocking();
     * @endcode
     */
    EmulatorGrpcClientBuilder& withEndpoint(const Endpoint& endpoint);
    /**
     * @brief Adds a gRPC interceptor to the client.
     *
     * Interceptors can be used to observe or modify RPC calls. This is useful
     * for implementing cross-cutting concerns like logging, authentication, or
     * metrics collection. You can add multiple interceptors, and they will be
     * executed in the order they are added.
     *
     * @param factory A unique pointer to the interceptor factory.
     * @return A reference to the builder for chaining.
     *
     * @par Example: Adding a Logging Interceptor
     * @code
     *   #include "google/protobuf/android/control/interceptor/logging_interceptor.h"
     *
     *   // ...
     *
     *   auto client = EmulatorGrpcClientBuilder()
     *       .withEndpoint(endpoint)
     *       .withInterceptor(std::make_unique<
     *           android::control::interceptor::StdOutLoggingInterceptorFactory>())
     *       .buildBlocking();
     * @endcode
     */
    EmulatorGrpcClientBuilder& withInterceptor(
            std::unique_ptr<ClientInterceptorFactoryInterface> factory);

    /**
     * @brief Builds a `BlockingEmulatorGrpcClient`.
     *
     * This method constructs a synchronous client based on the configuration
     * provided to the builder. The returned client will be in the
     * `Disconnected` state. You must call `connect()` before making any RPC
     * calls.
     *
     * @return A `StatusOr` containing either the client instance or an error
     *         if the configuration was invalid.
     */
    absl::StatusOr<std::unique_ptr<BlockingEmulatorGrpcClient>> buildBlocking();

    /**
     * @brief Builds a `CallbackEmulatorGrpcClient`.
     *
     * This method constructs an asynchronous, callback-based client. The
     * returned client will be in the `Disconnected` state. You must call
     * `connectAsync()` to initiate a connection.
     *
     * @return A `StatusOr` containing either the client instance or an error
     *         if the configuration was invalid.
     */
    absl::StatusOr<std::unique_ptr<CallbackEmulatorGrpcClient>> buildCallback();

  private:
    absl::Status mStatus{absl::OkStatus()};
    InterceptorFactories mFactories;
    Endpoint mDestination;
};

}  // namespace control
}  // namespace emulation
}  // namespace android