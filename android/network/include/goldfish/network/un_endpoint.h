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

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <winsock2.h>
#include <ws2def.h>
#include <afunix.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#endif
// IWYU pragma: end_keep
// clang-format on

#include "absl/status/statusor.h"

namespace goldfish::network {

struct UnEndpoint {
    static constexpr size_t kMaxSize =
            sizeof(static_cast<const struct sockaddr_un*>(nullptr)->sun_path);

    [[nodiscard]] const std::string& Address() const { return addr_; }
    static UnEndpoint MakeEmpty();
    static absl::StatusOr<UnEndpoint> Create(const std::filesystem::path&);
    static absl::StatusOr<UnEndpoint> Create(std::string addr);
    static UnEndpoint Create(const struct sockaddr_un& sun);

    bool operator==(const UnEndpoint& rhs) const { return addr_ == rhs.addr_; }

    UnEndpoint(const UnEndpoint&) = default;
    UnEndpoint(UnEndpoint&&) = default;
    UnEndpoint& operator=(const UnEndpoint&) = default;
    UnEndpoint& operator=(UnEndpoint&&) = default;

  private:
    UnEndpoint() = default;
    explicit UnEndpoint(std::string addr) : addr_(std::move(addr)) {}

    std::string addr_;
};

inline absl::StatusOr<UnEndpoint> ToUnEndpoint(std::string addr) {
    return UnEndpoint::Create(std::move(addr));
}

inline UnEndpoint ToUnEndpoint(const struct sockaddr_un& sun) {
    return UnEndpoint::Create(sun);
}

std::string ToString(const UnEndpoint&);

struct sockaddr_un ToSockaddr(const UnEndpoint&);

}  // namespace goldfish::network
