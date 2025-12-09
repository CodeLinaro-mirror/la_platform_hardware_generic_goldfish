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

#include <memory>
#include <vector>

#include "grpc_endpoint_description.pb.h"

namespace android {
namespace emulation {
namespace control {

using ::android::emulation::remote::Endpoint;
using grpc::experimental::ClientInterceptorFactoryInterface;
using InterceptorFactory = std::unique_ptr<ClientInterceptorFactoryInterface>;
using InterceptorFactories = std::vector<InterceptorFactory>;

/**
 * @brief A factory for creating gRPC channels.
 *
 * This class encapsulates the logic for creating a gRPC channel from an
 * Endpoint definition, including TLS configuration and header injection. It
 * simplifies the process of setting up a secure and functional gRPC connection.
 */
class GrpcChannelFactory {
  public:
    /**
     * @brief Constructs a GrpcChannelFactory.
     *
     * @param endpoint The protocol buffer definition of the gRPC endpoint,
     *                 containing target address, TLS configuration, and any
     *                 required headers.
     * @param interceptors A list of client interceptor factories to be applied
     *                     to the created channel, allowing for custom logic like
     *                     logging or metrics.
     */
    explicit GrpcChannelFactory(const Endpoint& endpoint, InterceptorFactories interceptors);
    ~GrpcChannelFactory() = default;

    /**
     * @brief Creates and returns a new gRPC channel.
     *
     * The channel is configured based on the Endpoint provided at construction,
     * including any TLS settings and interceptors.
     *
     * @return A shared pointer to the created grpc::Channel.
     */
    std::shared_ptr<grpc::Channel> createChannel();

    /**
     * @brief Retrieves the call credentials for the channel.
     *
     * If the endpoint requires authentication (e.g., via headers), this method
     * provides the necessary credentials. The credentials are created on the
     * first call and cached for subsequent use.
     *
     * @return A shared pointer to the grpc::CallCredentials, or nullptr if no
     *         credentials are required.
     */
    std::shared_ptr<grpc::CallCredentials> credentials() const;

  private:
    Endpoint mEndpoint;
    InterceptorFactories mInterceptors;
    mutable std::shared_ptr<grpc::CallCredentials> mCredentials;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
