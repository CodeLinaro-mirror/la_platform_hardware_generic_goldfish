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

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include "goldfish/network/ip_address.h"

namespace goldfish::network {

/**
 * @brief Represents a single, validated, resolved network endpoint.
 *
 * This class is a combination of a validated IpAddress and a port number.
 */
class Endpoint {
  public:
    /**
     * @brief Creates a valid Endpoint from an IpAddress and a port.
     *
     * @param ip_address A valid IpAddress object.
     * @param port The port number for the endpoint.
     * @return A valid Endpoint object.
     */
    Endpoint(IpAddress ip_address, int port);

    /**
     * @brief Creates a valid Endpoint from  a numeric IP address string and a port.
     *
     * @param addr A string_view to a valid numeric IP address string
     * @param port The port number for the endpoint.
     * @return absl::StatusOr<Endpoint> A valid Endpoint on success.
     */
    static absl::StatusOr<Endpoint> Create(std::string_view ip_address, int port);

    /**
     * @brief Creates a valid Endpoint from a generic sockaddr struct.
     *
     * This factory parses a sockaddr, extracting both the IP address
     * and the port in a single pass.
     *
     * @param sa A pointer to a valid sockaddr (e.g., from getaddrinfo).
     * @return absl::StatusOr<Endpoint> An Endpoint on success, or an error.
     */
    static absl::StatusOr<Endpoint> FromSockAddr(const struct sockaddr* sa);

    /**
     * @brief Returns the underlying IpAddress object.
     * @return const IpAddress& A reference to the IpAddress object.
     */
    [[nodiscard]] const IpAddress& Address() const { return ip_address_; }

    /**
     * @brief Returns the port number.
     * @return int The port number.
     */
    [[nodiscard]] int Port() const { return port_; }

    /**
     * @brief Returns a string representation of the endpoint.
     *
     * For IPv4, this will be in the format "ip:port".
     * For IPv6, this will be in the format "[ip]:port".
     *
     * @return std::string The formatted endpoint string.
     */
    [[nodiscard]] std::string ToString() const;

    /**
     * @brief Converts the Endpoint to a generic sockaddr_storage struct.
     *
     * This is useful for interoperability with low-level socket APIs.
     *
     * @return sockaddr_storage The converted socket address.
     */
    [[nodiscard]] sockaddr_storage ToSockaddr() const;

    bool operator==(const Endpoint& other) const {
        return ip_address_ == other.ip_address_ && port_ == other.port_;
    }

  private:
    IpAddress ip_address_;
    int port_;
};

template <typename Sink>
void AbslStringify(Sink& sink, const Endpoint& endpoint) {
    sink.Append(endpoint.ToString());
}

}  // namespace goldfish::network
