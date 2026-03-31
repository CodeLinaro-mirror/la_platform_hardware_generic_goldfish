// Copyright (C) 2023 The Android Open Source Project
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

#include <cstdint>

#include "grpc_diagnostic.pb.h"

#include "goldfish/circular_message_log.h"

namespace android::control::interceptor {

using grpc::experimental::ClientRpcInfo;
using grpc::experimental::Interceptor;
using grpc::experimental::InterceptorBatchMethods;
using grpc::experimental::ServerRpcInfo;

/**
 * @brief Intercepts gRPC calls to record structured diagnostic breadcrumbs.
 *
 * BreadcrumbInterceptor captures the lifecycle of an RPC (Start, Finish,
 * Message exchange, etc.) and stores it as structured binary Protobuf
 * in a circular buffer. This buffer is backed by a Crashpad Annotation,
 * ensuring it is preserved in minidumps.
 */
class BreadcrumbInterceptor : public grpc::experimental::Interceptor {
  public:
    explicit BreadcrumbInterceptor(const ClientRpcInfo* info);
    explicit BreadcrumbInterceptor(const ServerRpcInfo* info);
    ~BreadcrumbInterceptor() override;

    void Intercept(InterceptorBatchMethods* methods) override;

    /** @brief Returns the log instance for testing purposes. */
    static goldfish::proto_data_store::ProtoCircularLog<
            ::android::control::interceptor::GrpcBreadcrumb>*
    GetLogForTesting();

  private:
    uint32_t call_id_;
    uint32_t method_hash_;
};

/**
 * @brief Factory for creating BreadcrumbInterceptors.
 */
class BreadcrumbInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface,
                                     public grpc::experimental::ClientInterceptorFactoryInterface {
  public:
    BreadcrumbInterceptorFactory();
    ~BreadcrumbInterceptorFactory() override = default;
    Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override;
    Interceptor* CreateClientInterceptor(ClientRpcInfo* info) override;
};

}  // namespace android::control::interceptor
