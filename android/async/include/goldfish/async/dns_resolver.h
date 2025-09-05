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
#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <string>
#include <vector>

namespace goldfish::async {
/**
 * @brief Resolves a string address into a list of sockaddr_storage structs.
 *
 * This function takes a string address, such as "localhost:8080" or
 * "127.0.0.1:8080", and resolves it into a vector of sockaddr_storage structs
 * that can be used to create and bind sockets.
 *
 * @param address The address to resolve.
 * @param ai_flags Flags to pass to getaddrinfo(). These flags modify the
 * behavior of the address resolution process. Common values include:
 *
 * - **AI_PASSIVE**: Used with `getaddrinfo()` for socket binding. If the node is NULL, the returned
 * address will be suitable for a passive socket (for binding) listening for incoming connections.
 * - **AI_CANONNAME**: Requests the canonical name of the host. If this flag is set, the
 * `ai_canonname` field of the first `addrinfo` structure returned will be set to a C string
 * containing the canonical name of the host.
 * - **AI_NUMERICHOST**: Prevents a host name lookup. This is useful for performance when you
 * already know the address is a numeric one (e.g., "127.0.0.1").
 * - **AI_V4MAPPED**: Allows IPv6-enabled applications to use IPv4-mapped IPv6 addresses.
 *
 * @note In most simple client-side use cases where you are connecting to a remote host by name or
 * IP address, a value of 0 is sufficient.
 * @return A vector of sockaddr_storage structs. If the address cannot be
 * resolved, the vector will be empty.
 */
std::vector<sockaddr_storage> resolveAddress(const std::string& address, int ai_flags);

}  // namespace goldfish::async