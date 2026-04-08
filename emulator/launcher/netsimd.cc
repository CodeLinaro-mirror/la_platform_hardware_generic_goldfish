// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "netsimd.h"

#include <filesystem>

#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "android/status/status_macros.h"
#include "android/cmdline_option.h"
#include "android/goldfish/ini_file.h"
#include "goldfish/async/launch_config.h"
#include "goldfish/network/dns_resolver.h"

namespace fs = std::filesystem;
namespace android::goldfish {

namespace {

fs::path getEnvDir(const char* envvar, std::string_view subdir) {
    if (char* env_p = std::getenv(envvar); env_p && *env_p) {
        return fs::path(env_p) / subdir;
    }
    LOG(WARNING) << "No discovery env for " << envvar << ", using tmp/";
    return fs::path("/tmp");
}

fs::path GetNetsimDiscoveryDir() {
    // $TMPDIR is the temp directory on buildbots (and Mac).
    const char* test_env_p = std::getenv("TMPDIR");
    if (test_env_p && *test_env_p) {
        return fs::path(test_env_p);
    }
#if defined(_WIN32)
    return getEnvDir("LOCALAPPDATA", "Temp");
#elif defined(__linux__)
    return getEnvDir("ANDROID_EMULATOR_DISCOVERY_DIR", "");
#elif defined(__APPLE__)
    return getEnvDir("HOME", "Library/Caches/TemporaryItems");
#else
#error This platform is not supported.
#endif
}

}  // namespace

int read_netsim_port() {
    // TODO(whollins): Resolve this path with the others in launcher.cpp.
    // IniFile netsim_ini(mResolvedPaths.discovery_directory.parent_path().parent_path() /
    // "netsim.ini");
    IniFile netsim_ini(GetNetsimDiscoveryDir() / "netsim.ini");
    if (!netsim_ini.Read()) {
        VLOG(1) << "Failed to read netsim.ini";
        return 0;
    }
    return netsim_ini.GetInt("grpc.port", 0);
}

absl::StatusOr<NetsimConnection_ptr> connect_to_netsim(const std::string& endpoint,
                                                       absl::Duration connection_deadline) {
    android::emulation::control::Endpoint endpoint_config;
    endpoint_config.set_target(endpoint);

    ASSIGN_OR_RETURN(auto client, android::emulation::control::EmulatorGrpcClientBuilder()
                                          .WithEndpoint(endpoint_config)
                                          .BuildBlocking());
    RETURN_IF_ERROR(client->Connect(connection_deadline));
    return client;
}

absl::StatusOr<::goldfish::async::LaunchConfig> netsimd_launch_config(
        const std::filesystem::path& netsim_binary, const AndroidOptions& opts) {
    bool no_cli_ui = false;  //! feature_is_enabled(kFeature_NetsimCliUi),
    bool no_web_ui = true;   //! feature_is_enabled(kFeature_NetsimWebUi),
    std::string host_dns = opts.dns_server ? opts.dns_server : "";
    if (host_dns.empty()) {
        if (auto al = ::goldfish::network::GetSystemDnsServers(); al.ok()) {
            host_dns = absl::StrJoin(al.value(), ",", [](std::string* out, const auto& ip) {
                absl::StrAppend(out, ::goldfish::network::ToString(ip));
            });
        } else {
            LOG(WARNING) << "Failed to retrieve the system DNS servers due to: " << al.status();
            LOG(WARNING) << "The network simulation will run with reduced functionality.";
        }
        VLOG(1) << "Netsim DNS set to: " << host_dns;
    }
    std::string_view http_proxy = opts.http_proxy ? opts.http_proxy : "";
    std::string_view netsim_args = opts.netsim_args ? opts.netsim_args : "";

    std::vector<std::string> args;
    if (no_cli_ui) {
        args.push_back("--no-cli-ui");
    }
    if (no_web_ui) {
        args.push_back("--no-web-ui");
    }
    if (!host_dns.empty()) {
        args.push_back(absl::StrCat("--host-dns=", host_dns));
    }
    if (!http_proxy.empty()) {
        args.push_back(absl::StrCat("--http-proxy=", http_proxy));
    }

    if (opts.verbose) {
        args.push_back("--logtostderr");
    }

    for (auto& flag : absl::StrSplit(netsim_args, " ", absl::SkipEmpty())) {
        args.push_back(std::string(flag));
    }

    LOG(INFO) << "Netsimd launch command: " << netsim_binary << " " << absl::StrJoin(args, " ");
    return ::goldfish::async::LaunchConfig{
        .exe_path = netsim_binary,
        .args = args,
        //.environment = {},
        .daemon = true,
        .new_process_group = false,
        .keep_stdio = opts.netsim_stdout != 0,
    };
}

}  // namespace android::goldfish
