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
#include "goldfish/async/dns_resolver.h"

#include <vector>

#include "absl/log/log.h"

namespace goldfish::async {

std::vector<sockaddr_storage> resolveAddress(const std::string& address, int ai_flags) {
    std::vector<sockaddr_storage> addresses;
    size_t c = address.find_last_of(':');
    if (c == std::string::npos) {
        LOG(ERROR) << "Invalid address format, missing ':': " << address;
        return addresses;
    }
    std::string host = address.substr(0, c);
    std::string port_str = address.substr(c + 1);

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = ai_flags;

    struct addrinfo* result;
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
        LOG(ERROR) << "Failed to resolve address: " << address;
        return addresses;
    }

    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sockaddr_storage addr;
        memcpy(&addr, rp->ai_addr, rp->ai_addrlen);
        addresses.push_back(addr);
    }
    freeaddrinfo(result);
    return addresses;
}

}  // namespace goldfish::async
