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

#include <string>
#include <string_view>
#include <variant>

#include "absl/status/statusor.h"

#include "goldfish/network/ip_endpoint.h"
#include "goldfish/network/un_endpoint.h"

namespace goldfish::network {

using Endpoint = std::variant<Ipv4Endpoint, Ipv6Endpoint, UnEndpoint>;

absl::StatusOr<Endpoint> ToEndpoint(const struct sockaddr&);

Endpoint ToEndpoint(const IpAddress& addr, uint16_t port);

std::string ToString(const Endpoint&);

struct sockaddr_storage ToSockaddr(const Endpoint&);

int GetPortFromEndpoint(const Endpoint&);

struct EndpointFormatter {
    void operator()(std::string* out, const Endpoint& e) const { out->append(ToString(e)); }
};

}  // namespace goldfish::network
