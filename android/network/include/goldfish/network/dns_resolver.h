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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif
#include <string>
#include <vector>

#include "goldfish/network/endpoint.h"
#include "goldfish/network/ip_address.h"

namespace goldfish::network {

/**
 * @brief Resolves a string address into a list of Endpoint objects.
 *
 * This function takes a string address, such as "localhost:8080" or
 * "[::1]:8080", and resolves it into a vector of Endpoint objects.
 * It is a high-level, RAII-safe wrapper around the getaddrinfo C-API,
 * guaranteeing resource cleanup and propagating errors.
 *
 * @param hostname The hostname to resolve (e.g., "localhost").
 * @param hints An optional pointer to a struct addrinfo with additional address type constraints
 * (e.g., ai_family = AF_INET6). If nullptr, default hints (AF_UNSPEC, SOCK_STREAM) are used.
 * @return An absl::StatusOr<std::vector<IpAddress>> with three possible outcomes:
 * * **Ok + Non-empty vector:** The lookup succeeded and found one or more valid IPv4/IPv6
 * addresses.
 * * **Non-Ok Status:** The DNS lookup itself failed (e.g., absl::StatusCode::kNotFound if the host
 * does not exist).
 */
absl::StatusOr<std::vector<Endpoint>> ResolveEndpoints(const std::string& address,
                                                       const struct addrinfo* hints = nullptr);

/**
 * @brief Resolves a hostname to a list of IpAddress objects.
 *
 * This is a high-level, RAII-safe wrapper around the getaddrinfo C-API
 * that is only concerned with IP addresses and ignores any port information.
 *
 * @param hostname The hostname to resolve (e.g., "localhost").
 * @param hints An optional pointer to a struct addrinfo with additional address type constraints
 * (e.g., ai_family = AF_INET6). If nullptr, default hints (AF_UNSPEC, SOCK_STREAM) are used.
 * @return An absl::StatusOr<std::vector<IpAddress>> with three possible outcomes:
 * * **Ok + Non-empty vector:** The lookup succeeded and found one or more valid IPv4/IPv6
 * addresses.
 * * **Non-Ok Status:** The DNS lookup itself failed (e.g., absl::StatusCode::kNotFound if the host
 * does not exist).
 */
absl::StatusOr<std::vector<IpAddress>> ResolveHostname(const std::string& hostname,
                                                       const struct addrinfo* hints = nullptr);

/**
 * @brief Retrieves the list of upstream DNS servers configured on the host system.
 *
 * This function queries the underlying operating system (via c-ares) to determine
 * the active DNS resolvers.
 *
 * @return An absl::StatusOr<std::vector<IpAddress>> containing the list of unique
 * DNS server addresses found. Returns kNotFound if no servers could be determined.
 */
absl::StatusOr<std::vector<IpAddress>> GetSystemDnsServers();

}  // namespace goldfish::network