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
#include "goldfish/network/un_endpoint.h"

namespace goldfish::network {

UnEndpoint UnEndpoint::MakeEmpty() {
    return {};
}

absl::StatusOr<UnEndpoint> UnEndpoint::Create(const std::filesystem::path& path) {
    return Create(path.string());
}

absl::StatusOr<UnEndpoint> UnEndpoint::Create(std::string addr) {
    // we need one extra character to store zero
    if (addr.size() > kMaxSize) {
        return absl::InvalidArgumentError(absl::StrFormat("The address is too long: '%s'", addr));
    }

    return UnEndpoint(std::move(addr));
}

UnEndpoint UnEndpoint::Create(const struct sockaddr_un& sun) {
    if (sun.sun_path[0]) {
        return UnEndpoint(std::string(sun.sun_path, ::strnlen(sun.sun_path, kMaxSize)));
    }

    for (size_t z = kMaxSize - 1; z > 0; --z) {
        if (sun.sun_path[z]) {
            return UnEndpoint(std::string(sun.sun_path, z + 1));
        }
    }

    return {};  // we already checked sun_path[0] above
}

std::string ToString(const UnEndpoint& endpoint) {
    return endpoint.Address();
}

struct sockaddr_un ToSockaddr(const UnEndpoint& endpoint) {
    const std::string& addr = endpoint.Address();

    struct sockaddr_un sun = {};
    sun.sun_family = AF_UNIX;
    ::memcpy(&sun.sun_path[0], addr.data(), addr.size());
    ::memset(&sun.sun_path[addr.size()], 0, sizeof(sun.sun_path) - addr.size());
    return sun;
}

}  // namespace goldfish::network