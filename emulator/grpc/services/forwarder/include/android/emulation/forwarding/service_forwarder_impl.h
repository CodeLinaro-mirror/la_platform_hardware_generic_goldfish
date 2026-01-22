// Copyright 2026 The Android Open Source Project
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

#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"

#include "service_forwarder.grpc.pb.h"

namespace android::emulation::forwarding {

using android::emulation::remote::Endpoint;

class ServiceForwarderImpl final : public ServiceForwarder::Service {
  public:
    grpc::Status registerForwarder(grpc::ServerContext* context, const ForwardingRule* request,
                                   google::protobuf::Empty* reply) override;
    grpc::Status listForwardingRules(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     ForwardingRuleList* reply) override;

    // Thread-safe lookup for a service URI.
    std::optional<Endpoint> GetEndpoint(const std::string& service_uri) const;

  private:
    mutable absl::Mutex mutex_;
    absl::flat_hash_map<std::string, Endpoint> rules_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace android::emulation::forwarding
