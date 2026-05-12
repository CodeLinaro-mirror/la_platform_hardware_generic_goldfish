// Copyright (C) 2026 The Android Open Source Project
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

#include <atomic>
#include <cstdint>
#include <string>

namespace android::control::interceptor {

class PerfettoInterceptor : public grpc::experimental::Interceptor {
  public:
    PerfettoInterceptor(grpc::experimental::ServerRpcInfo* info);
    PerfettoInterceptor(grpc::experimental::ClientRpcInfo* info);
    ~PerfettoInterceptor() override;

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override;

  private:
    const uint64_t cookie_;
    const std::string method_name_;
};

class PerfettoInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface,
                                   public grpc::experimental::ClientInterceptorFactoryInterface {
  public:
    PerfettoInterceptorFactory() = default;
    ~PerfettoInterceptorFactory() override = default;
    grpc::experimental::Interceptor* CreateServerInterceptor(
            grpc::experimental::ServerRpcInfo* info) override;
    grpc::experimental::Interceptor* CreateClientInterceptor(
            grpc::experimental::ClientRpcInfo* info) override;
};

}  // namespace android::control::interceptor
