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
#include "absl/log/log.h"
#include "absl/strings/str_join.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/network/dns_resolver.h"

namespace goldfish::async {

using network::EndpointFormatter;

std::shared_ptr<AsyncSocket> createSocketFromHostname(AsyncSocketFactory& factory, EventLoop* loop,
                                                      const std::string& hostname) {
    auto endpoints = goldfish::network::ResolveEndpoints(hostname);
    if (!endpoints.ok()) {
        return nullptr;
    }
    VLOG(1) << "Resolved: " << hostname << " to: ["
            << absl::StrJoin(endpoints.value(), ", ", EndpointFormatter()) << "]";
    for (const auto& endpoint : endpoints.value()) {
        if (auto socket = factory.createSocket(loop, endpoint)) {
            return socket;
        }
    }
    return nullptr;
}

std::shared_ptr<AsyncSocketServer> createServerFromHostname(
        AsyncSocketFactory& factory, EventLoop* loop, const std::string& hostname,
        AsyncSocketServer::ConnectCallback connectCallback) {
    auto endpoints = goldfish::network::ResolveEndpoints(hostname);
    if (!endpoints.ok()) {
        return nullptr;
    }
    VLOG(1) << "Resolved: " << hostname << " to: ["
            << absl::StrJoin(endpoints.value(), ", ", EndpointFormatter()) << "]";
    for (const auto& endpoint : endpoints.value()) {
        if (auto server = factory.createServer(loop, endpoint, connectCallback)) {
            return server;
        }
    }
    return nullptr;
}

}  // namespace goldfish::async