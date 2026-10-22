// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file netsim_discovery.h
 * @brief Netsim endpoint discovery and server address parsing helpers.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

#include "emulator/plugin/netsim/packet_transport.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::netsim {

/**
 * @brief Returns the path to the netsim discovery INI file for the given
 *        instance (`netsim.ini` for instance 1, `netsim_<N>.ini` otherwise).
 * @param instance 1-based netsim daemon instance number.
 * @return Filesystem path to the discovery INI file.
 */
std::filesystem::path IniFilePath(uint16_t instance);

/**
 * @brief Reads a server address entry from the netsim discovery INI file.
 * @param instance 1-based netsim daemon instance number.
 * @param key INI property key to query (e.g. `"http.port"` or `"grpc.port"`).
 * @return Normalized `"host:port"` address string, or an error status if the
 *         file is missing, unreadable, or lacks `key`.
 */
absl::StatusOr<std::string> ServerAddressFromIni(uint16_t instance, std::string_view key);

/**
 * @brief Reads a server entry from the netsim discovery INI file and resolves
 *        it into one or more `goldfish::network::Endpoint` values.
 * @param instance 1-based netsim daemon instance number.
 * @param key INI property key to query (e.g. `"http.port"` or `"grpc.port"`).
 * @return Non-empty list of resolved `Endpoint`s (e.g. IPv4/IPv6 loopback
 *         endpoints when `netsim.ini` specifies a port), or an error status.
 */
absl::StatusOr<std::vector<goldfish::network::Endpoint>> ServerEndpointsFromIni(
        uint16_t instance, std::string_view key);

/**
 * @brief Resolves the target server address from `options.server_address` or
 *        falls back to `ServerAddressFromIni(options.instance, ini_key)`.
 * @param options Transport configuration options.
 * @param ini_key Discovery INI key used when `options.server_address` is empty.
 * @return Resolved server address string, or an error status on failure.
 */
absl::StatusOr<std::string> ResolveServerAddress(const TransportOptions& options,
                                                 std::string_view ini_key);

/**
 * @brief Parses and resolves a server address string into one or more
 *        `goldfish::network::Endpoint` objects.
 * @param address Server address string (`"host:port"`, `"[ipv6]:port"`, or `"host"`).
 * @return Non-empty list of resolved `Endpoint`s, or an error status on failure.
 */
absl::StatusOr<std::vector<goldfish::network::Endpoint>> ResolveServerEndpoints(
        std::string_view address);

/**
 * @brief Splits a server address (`"host:port"`, `"[ipv6]:port"`, or `"host"`)
 *        into host and port components (defaulting port to `"80"` when omitted).
 * @param address Server address string to parse.
 * @return Pair of `(host, port)` strings, or `InvalidArgumentError` if malformed.
 */
absl::StatusOr<std::pair<std::string, std::string>> ParseAddress(std::string_view address);

}  // namespace goldfish::netsim
