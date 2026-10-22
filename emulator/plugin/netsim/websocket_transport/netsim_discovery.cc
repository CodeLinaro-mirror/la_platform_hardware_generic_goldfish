// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/websocket_transport/netsim_discovery.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/base/system.h"
#include "android/goldfish/ini_file.h"
#include "goldfish/file/file.h"
#include "goldfish/network/dns_resolver.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::netsim {
namespace {

#if defined(_WIN32)
constexpr char kRootEnv[] = "LOCALAPPDATA";
constexpr char kSubdir[] = "Temp";
constexpr char kDiscoveryEnvHint[] = "TMPDIR, LOCALAPPDATA";
#elif defined(__APPLE__)
constexpr char kRootEnv[] = "HOME";
constexpr char kSubdir[] = "Library/Caches/TemporaryItems";
constexpr char kDiscoveryEnvHint[] = "TMPDIR, HOME";
#else
constexpr char kRootEnv[] = "XDG_RUNTIME_DIR";
constexpr char kSubdir[] = "";
constexpr char kDiscoveryEnvHint[] = "TMPDIR, ANDROID_EMULATOR_DISCOVERY_DIR, XDG_RUNTIME_DIR";
#endif

std::filesystem::path DiscoveryDir() {
    auto* sys = android::base::System::Get();
    if (std::string tmpdir = sys->EnvGet("TMPDIR"); !tmpdir.empty()) {
        return std::filesystem::path(tmpdir);
    }
#if !defined(_WIN32) && !defined(__APPLE__)
    if (std::string discovery_dir = sys->EnvGet("ANDROID_EMULATOR_DISCOVERY_DIR");
        !discovery_dir.empty()) {
        return std::filesystem::path(discovery_dir);
    }
#endif
    std::string root = sys->EnvGet(kRootEnv);
    std::filesystem::path base = !root.empty() ? std::filesystem::path(root) : "/tmp";
    return base / kSubdir;
}

}  // namespace

std::filesystem::path IniFilePath(uint16_t instance) {
    // Filename convention matches netsimd (tools/netsim/next/common/src/util/ini_file.rs
    // and emulator/launcher/launch_netsimd.cc): "netsim.ini" for instance 1, and
    // "netsim_<instance>.ini" for additional instances directly under DiscoveryDir().
    std::string name = (instance == 1) ? "netsim.ini" : absl::StrCat("netsim_", instance, ".ini");
    return DiscoveryDir() / name;
}

absl::StatusOr<std::string> ServerAddressFromIni(uint16_t instance, std::string_view key) {
    std::filesystem::path path = IniFilePath(instance);
    if (!android::base::file::exists(path)) {
        return absl::NotFoundError(absl::StrFormat(
                "Netsim discovery file '%s' for instance %d does not exist; ensure netsimd is "
                "running or set TransportOptions::server_address (checked %s)",
                path.string(), instance, kDiscoveryEnvHint));
    }
    android::goldfish::IniFile ini(path);
    if (!ini.Read()) {
        return absl::UnavailableError(absl::StrFormat(
                "Failed to read or parse netsim discovery INI file '%s' for instance %d",
                path.string(), instance));
    }
    std::string value = ini.GetString(std::string(key), "");
    if (value.empty()) {
        return absl::NotFoundError(absl::StrFormat(
                "Netsim discovery file '%s' (instance %d) is missing required non-empty key '%s' "
                "(expected 'host:port' or port number, e.g. 'localhost:8554' or '8554')",
                path.string(), instance, key));
    }
    return value.find(':') != std::string::npos ? value : absl::StrCat("localhost:", value);
}

absl::StatusOr<std::vector<goldfish::network::Endpoint>> ServerEndpointsFromIni(
        uint16_t instance, std::string_view key) {
    absl::StatusOr<std::string> address = ServerAddressFromIni(instance, key);
    if (!address.ok()) return address.status();
    return ResolveServerEndpoints(*address);
}

absl::StatusOr<std::string> ResolveServerAddress(const TransportOptions& options,
                                                 std::string_view ini_key) {
    if (!options.server_address.empty()) return options.server_address;
    return ServerAddressFromIni(options.instance, ini_key);
}

absl::StatusOr<std::vector<goldfish::network::Endpoint>> ResolveServerEndpoints(
        std::string_view address) {
    absl::StatusOr<std::pair<std::string, std::string>> parsed = ParseAddress(address);
    if (!parsed.ok()) return parsed.status();
    auto [host, port] = *std::move(parsed);
    std::string normalized = host.find(':') != std::string::npos
                                     ? absl::StrCat("[", host, "]:", port)
                                     : absl::StrCat(host, ":", port);
    absl::StatusOr<std::vector<goldfish::network::Endpoint>> endpoints =
            goldfish::network::ResolveEndpoints(normalized);
    if (!endpoints.ok()) return endpoints.status();
    if (endpoints->empty()) {
        return absl::NotFoundError(absl::StrFormat(
                "No network endpoints resolved for server address '%s' (normalized='%s'); "
                "verify the hostname or IP address configuration",
                address, normalized));
    }
    return endpoints;
}

absl::StatusOr<std::pair<std::string, std::string>> ParseAddress(std::string_view address) {
    if (address.empty()) {
        return absl::InvalidArgumentError(
                "Malformed server address '': expected non-empty 'host' or 'host:port' "
                "(e.g. '127.0.0.1:8554')");
    }
    std::string host(address), port = "80";
    if (address.front() == '[') {
        size_t close_bracket = address.find(']');
        if (close_bracket == std::string_view::npos || close_bracket <= 1) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Malformed IPv6 server address '%s': expected non-empty '[ipv6]' or "
                    "'[ipv6]:port' (e.g. '[::1]:8554')",
                    address));
        }
        host = std::string(address.substr(1, close_bracket - 1));
        if (close_bracket + 1 < address.size()) {
            if (address[close_bracket + 1] != ':' || close_bracket + 2 >= address.size()) {
                return absl::InvalidArgumentError(absl::StrFormat(
                        "Malformed IPv6 server address '%s': expected ':port' after ']' "
                        "(e.g. '[::1]:8554')",
                        address));
            }
            port = std::string(address.substr(close_bracket + 2));
        }
    } else if (size_t colon = address.rfind(':');
               colon != std::string_view::npos && address.find(':') == colon) {
        host = std::string(address.substr(0, colon));
        port = std::string(address.substr(colon + 1));
        if (host.empty() || port.empty()) {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Malformed server address '%s': expected non-empty 'host:port' "
                                    "(e.g. '127.0.0.1:8554')",
                                    address));
        }
    }
    return std::make_pair(std::move(host), std::move(port));
}

}  // namespace goldfish::netsim
