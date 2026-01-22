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

#include "android/emulation/forwarding/service_forwarder_impl.h"

#include "absl/log/log.h"

namespace android::emulation::forwarding {

using android::emulation::remote::Endpoint;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;

Status ServiceForwarderImpl::registerForwarder(ServerContext* /*context*/,
                                               const ForwardingRule* request, Empty* /*reply*/) {
    const absl::MutexLock lock(&mutex_);
    rules_[request->service_uri()] = request->endpoint();
    LOG(INFO) << "Registered forwarder for URI: " << request->service_uri()
              << " target: " << request->endpoint().target();
    return Status::OK;
}

Status ServiceForwarderImpl::listForwardingRules(ServerContext* /*context*/,
                                                 const Empty* /*request*/,
                                                 ForwardingRuleList* reply) {
    const absl::MutexLock lock(&mutex_);
    for (const auto& [uri, endpoint] : rules_) {
        auto* rule = reply->add_rules();
        rule->set_service_uri(uri);
        *rule->mutable_endpoint() = endpoint;
    }
    return Status::OK;
}

std::optional<Endpoint> ServiceForwarderImpl::GetEndpoint(const std::string& serviceUri) const {
    const absl::MutexLock lock(&mutex_);
    const auto it = rules_.find(serviceUri);
    if (it != rules_.end()) {
        return it->second;
    }
    return std::nullopt;
}

}  // namespace android::emulation::forwarding
