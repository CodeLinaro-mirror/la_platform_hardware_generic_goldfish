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

#include "goldfish/network/dns_resolver.h"

#include <ares.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

// Platform-specific C headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace goldfish::network {

namespace {

// --- getaddrinfo RAII helpers ---

struct AddrInfoDeleter {
    void operator()(struct addrinfo* p) const {
        if (p) {
            freeaddrinfo(p);
        }
    }
};

using AddrInfoPtr = std::unique_ptr<struct addrinfo, AddrInfoDeleter>;

// --- c-ares RAII helpers ---

struct AresChannelDeleter {
    void operator()(ares_channel c) const {
        if (c) {
            ares_destroy(c);
        }
    }
};

// Note: ares_channel is a typedef for struct ares_channeldata*
using AresChannelPtr = std::unique_ptr<struct ares_channeldata, AresChannelDeleter>;

struct AresDataDeleter {
    void operator()(void* p) const {
        if (p) {
            ares_free_data(p);
        }
    }
};

using AresServerListPtr = std::unique_ptr<struct ares_addr_node, AresDataDeleter>;

/**
 * @brief A helper that performs a DNS lookup, wraps the result in an
 * RAII-safe pointer, and processes the list with a lambda.
 *
 * @return absl::OkStatus() on success, or a NotFoundError on DNS failure.
 */
absl::Status ProcessAddrInfo(const std::string& host, const std::string& port,
                             const struct addrinfo* hints,
                             const std::function<void(const struct addrinfo&)>& processor) {
    struct addrinfo default_hints = {};
    if (!hints) {
        default_hints.ai_family = AF_UNSPEC;
        default_hints.ai_socktype = SOCK_STREAM;
        hints = &default_hints;
    }

    struct addrinfo* raw_result = nullptr;
    const int ret =
            getaddrinfo(host.c_str(), port.empty() ? nullptr : port.c_str(), hints, &raw_result);

    const AddrInfoPtr result(raw_result);

    // Handle DNS lookup failures
    if (ret != 0) {
        return absl::NotFoundError(
                absl::StrFormat("Failed to resolve address or hostname: %s%s, error: %s", host,
                                (port.empty() ? "" : (":" + port)), gai_strerror(ret)));
    }

    // Process the list using a simple, exception-safe loop
    for (auto* rp = result.get(); rp != nullptr; rp = rp->ai_next) {
        processor(*rp);
    }

    return absl::OkStatus();
}

struct HostPort {
    std::string host;
    std::string port;
};

/**
 * @brief Parses a network address string into host and port components.
 *
 * This function handles various address formats, including:
 * - "hostname:port" (e.g., "example.com:8080")
 * - "ipv4:port" (e.g., "127.0.0.1:8080")
 * - "[ipv6]:port" (e.g., "[::1]:8080")
 * - "hostname" (port will be empty)
 * - "ipv4" (port will be empty)
 * - "[ipv6]" (port will be empty)
 *
 * @param address The network address string to parse.
 * @return A HostPort struct containing the host and port as separate strings.
 * The port string will be empty if no port is found.
 */
HostPort ParseHostPort(std::string_view address) {
    std::string_view host = address;
    std::string_view port_str;
    const size_t colon_pos = address.rfind(':');
    const size_t bracket_pos = address.rfind(']');

    // Check for a port (a colon outside of IPv6 brackets)
    if (colon_pos != std::string_view::npos &&
        (colon_pos > bracket_pos || bracket_pos == std::string_view::npos)) {
        host = address.substr(0, colon_pos);
        port_str = address.substr(colon_pos + 1);
    }

    // Remove brackets from IPv6 literal
    if (!host.empty() && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.length() - 2);
    }
    return {.host = std::string(host), .port = std::string(port_str)};
}
}  // namespace

absl::StatusOr<std::vector<Endpoint>> ResolveEndpoints(const std::string& address,
                                                       const struct addrinfo* hints) {
    const HostPort hp = ParseHostPort(address);
    std::vector<Endpoint> endpoints;

    auto status = ProcessAddrInfo(hp.host, hp.port, hints, [&endpoints](const struct addrinfo& rp) {
        absl::StatusOr<Endpoint> endpoint = Endpoint::FromSockAddr(rp.ai_addr);
        if (endpoint.ok()) {
            endpoints.push_back(*endpoint);
        }
    });

    if (!status.ok()) {
        return status;
    }

    return endpoints;
}

absl::StatusOr<std::vector<IpAddress>> ResolveHostname(const std::string& hostname,
                                                       const struct addrinfo* hints) {
    std::vector<IpAddress> addresses;

    auto status =
            ProcessAddrInfo(hostname, /*port=*/"", hints, [&addresses](const struct addrinfo& rp) {
                absl::StatusOr<Endpoint> endpoint = Endpoint::FromSockAddr(rp.ai_addr);
                if (endpoint.ok()) {
                    addresses.push_back(endpoint->Address());
                }
            });

    if (!status.ok()) {
        return status;
    }

    return addresses;
}

absl::StatusOr<std::vector<IpAddress>> GetSystemDnsServers() {
    // TODO(whollins): only do this once globally.
    // Also: Cleanup c-ares library before exiting - ares_library_cleanup();
    if (const int status = ares_library_init(ARES_LIB_INIT_ALL); status != ARES_SUCCESS) {
        return absl::InternalError(
                absl::StrFormat("Failed to initialize c-ares library: %s", ares_strerror(status)));
    }

    ares_channel raw_channel = nullptr;
    if (const int err = ares_init(&raw_channel); err != ARES_SUCCESS) {
        return absl::InternalError(
                absl::StrFormat("Failed to initialize c-ares: %s", ares_strerror(err)));
    }
    const AresChannelPtr channel(raw_channel);

    struct ares_addr_node* raw_servers = nullptr;
    if (const int err = ares_get_servers(channel.get(), &raw_servers); err != ARES_SUCCESS) {
        return absl::InternalError(
                absl::StrFormat("Failed to retrieve DNS servers: %s", ares_strerror(err)));
    }
    const AresServerListPtr servers(raw_servers);

    // We have results! Let's parse them out.
    std::vector<IpAddress> results;
    for (auto* node = servers.get(); node != nullptr; node = node->next) {
        absl::StatusOr<IpAddress> ip;

        if (node->family == AF_INET) {
            ip = IpAddress::FromBinary(&node->addr.addr4);
        } else if (node->family == AF_INET6) {
            // c-ares IPv6 struct is layout-compatible with standard in6_addr
            ip = IpAddress::FromBinary(reinterpret_cast<const struct in6_addr*>(&node->addr.addr6));
        }

        // Very unlikely that the c-ares returns incorrect ip struct.
        DCHECK(ip.ok()) << "c-ares produced an incorrect IP address, this implies something is "
                           "wrong with c-ares.";
        if (ip.ok()) {
            results.emplace_back(*std::move(ip));
        }
    }

    if (results.empty()) {
        return absl::NotFoundError("No valid system DNS servers found.");
    }

    return results;
}

}  // namespace goldfish::network
