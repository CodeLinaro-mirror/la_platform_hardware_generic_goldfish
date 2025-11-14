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

#include <array>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

namespace goldfish::network {

/**
 * @brief Represents a single, validated, numeric IP address.
 */
class IpAddress {
  public:
    enum class Family {
        kIpv4,
        kIpv6,
    };

    /**
     * @brief Creates a valid IpAddress from a numeric IP address string.
     *
     * @param addr A string_view to a valid numeric IP address string
     * @return absl::StatusOr<IpAddress> A valid IpAddress on success.
     */
    static absl::StatusOr<IpAddress> create(std::string_view ip_address);

    /**
     * @brief Creates a valid IpAddress from a binary IPv4 address.
     *
     * @param addr A pointer to a valid struct in_addr.
     * @return absl::StatusOr<IpAddress> A valid IpAddress on success.
     */
    static absl::StatusOr<IpAddress> fromBinary(const struct in_addr* addr);

    /**
     * @brief Creates a valid IpAddress from a binary IPv6 address.
     *
     * @param addr A pointer to a valid struct in6_addr.
     */
    static absl::StatusOr<IpAddress> fromBinary(const struct in6_addr* addr);

    /**
     * @brief Returns the numeric IP address string.
     *
     * @return std::string The IP address (e.g., "127.0.0.1" or "::1").
     */
    std::string toString() const;

    /**
     * @brief Returns the address family.
     */
    Family family() const { return mFamily; }

    /**
     * @brief Checks if the address is an IPv4 address.
     */
    bool isIpv4() const { return mFamily == Family::kIpv4; }

    /**
     * @brief Checks if the address is an IPv6 address.
     */
    bool isIpv6() const { return mFamily == Family::kIpv6; }

    /**
     * @brief Provides a type-safe pointer to the binary IPv4 address.
     *
     * @return A pointer to the internal in_addr data if this is an
     * IPv4 address, otherwise nullptr.
     */
    const struct in_addr* asV4() const;

    /**
     * @brief Provides a type-safe pointer to the binary IPv6 address.
     *
     * @return A pointer to the internal in6_addr data if this is an
     * IPv6 address, otherwise nullptr.
     */
    const struct in6_addr* asV6() const;

  private:
    IpAddress(const in_addr* ipv4);
    IpAddress(const in6_addr* ipv6);

    Family mFamily;
    std::array<std::byte, sizeof(struct in6_addr)> mAddr;
};

template <typename Sink>
void AbslStringify(Sink& sink, const IpAddress& ip) {
    sink.Append(ip.toString());
}

}  // namespace goldfish::network