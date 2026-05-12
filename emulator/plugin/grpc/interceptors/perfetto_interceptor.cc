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
#include "android/control/interceptor/perfetto_interceptor.h"

#include "goldfish/perfetto/perfetto_categories.h"

namespace android::control::interceptor {

static std::atomic<uint64_t> s_cookie_counter_{1};

PerfettoInterceptor::PerfettoInterceptor(grpc::experimental::ServerRpcInfo* info)
        : cookie_(s_cookie_counter_.fetch_add(1, std::memory_order_relaxed))
        , method_name_(info ? std::string(info->method()) : "unknown") {
    TRACE_EVENT("grpc", "gRPC Call Start (Server)", perfetto::Flow::ProcessScoped(cookie_),
                "method", method_name_);
}

PerfettoInterceptor::PerfettoInterceptor(grpc::experimental::ClientRpcInfo* info)
        : cookie_(s_cookie_counter_.fetch_add(1, std::memory_order_relaxed))
        , method_name_(info ? std::string(info->method()) : "unknown") {
    TRACE_EVENT("grpc", "gRPC Call Start (Client)", perfetto::Flow::ProcessScoped(cookie_),
                "method", method_name_);
}

PerfettoInterceptor::~PerfettoInterceptor() {
    TRACE_EVENT("grpc", "gRPC Call End", perfetto::Flow::ProcessScoped(cookie_), "method",
                method_name_);
}

void PerfettoInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods) {
    TRACE_EVENT("grpc", "gRPC Intercept", perfetto::Flow::ProcessScoped(cookie_), "method",
                method_name_);
    methods->Proceed();
}

grpc::experimental::Interceptor* PerfettoInterceptorFactory::CreateServerInterceptor(
        grpc::experimental::ServerRpcInfo* info) {
    return new PerfettoInterceptor(info);
}

grpc::experimental::Interceptor* PerfettoInterceptorFactory::CreateClientInterceptor(
        grpc::experimental::ClientRpcInfo* info) {
    return new PerfettoInterceptor(info);
}

}  // namespace android::control::interceptor
