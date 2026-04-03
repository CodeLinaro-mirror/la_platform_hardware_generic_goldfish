/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/support/interceptor.h"

#include "android/control/interceptor/logging_interceptor.h"
#include "android/control/interceptor/percentiles.h"
#include "goldfish/metrics/metrics_reporter.h"

namespace android::control::interceptor {

class MetricsInterceptorFactory : public grpc::experimental::ServerInterceptorFactoryInterface {
  public:
    // Metrics for a single gRPC method.
    struct MethodMetrics {
        // Tracks bytes received for streaming reads.
        Percentiles rcv_bytes{32, {0.9}};

        // Tracks bytes sent for streaming writes.
        Percentiles snd_bytes{32, {0.9}};

        // Duration of the total call length.
        Percentiles duration{32, {0.9}};

        // Messages received.
        Percentiles rcv_msg{32, {0.9}};

        // Messages sent.
        Percentiles snd_msg{32, {0.9}};

        int total_requests = 0;
        int failed_requests = 0;
    };

    explicit MetricsInterceptorFactory(::goldfish::metrics::MetricsReporter& reporter)
            : reporter_(reporter) {}
    ~MetricsInterceptorFactory() override { ReportMetrics(); }

    grpc::experimental::Interceptor* CreateServerInterceptor(
            grpc::experimental::ServerRpcInfo* info) override {
        return new LoggingInterceptor(info,
                                      [this](const InvocationRecord& record) { Record(record); });
    }

    void Record(const InvocationRecord& invocation);

    void ReportMetrics() {
        absl::MutexLock lock(&mutex_);
        for (const auto& [name, metrics] : server_metrics_) {
            ReportMethodMetrics(name, metrics, /*is_server=*/true);
        }
        for (const auto& [name, metrics] : client_metrics_) {
            ReportMethodMetrics(name, metrics, /*is_server=*/false);
        }
    }

  private:
    void ReportMethodMetrics(const std::string& name, const MethodMetrics& metrics, bool is_server);

    ::goldfish::metrics::MetricsReporter& reporter_;

    absl::Mutex mutex_;
    absl::flat_hash_map<std::string, MethodMetrics> server_metrics_ ABSL_GUARDED_BY(mutex_);
    absl::flat_hash_map<std::string, MethodMetrics> client_metrics_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace android::control::interceptor
