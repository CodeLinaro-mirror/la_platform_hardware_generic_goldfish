// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/aquarium/aquarium_ui_server.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include "android/base/bazel_info.h"
#include "android/emulation/control/emulator_grpc_client.h"
#include "android/emulation/control/grpc_channel_factory.h"
#include "goldfish/http/http_response.h"
#include "goldfish/network/endpoint.h"

namespace fs = std::filesystem;

namespace goldfish::aquarium {

using ::android::emulation::control::EmulatorGrpcClientBuilder;
using ::android::emulation::control::GrpcChannelFactory;
using ::android::emulation::remote::Endpoint;

std::string_view GetMimeType(std::string_view path) {
    if (absl::EndsWith(path, ".html") || absl::EndsWith(path, ".htm"))
        return "text/html; charset=utf-8";
    if (absl::EndsWith(path, ".js") || absl::EndsWith(path, ".mjs") ||
        absl::EndsWith(path, ".ts") || absl::EndsWith(path, ".tsx") || absl::EndsWith(path, ".jsx"))
        return "application/javascript; charset=utf-8";
    if (absl::EndsWith(path, ".css")) return "text/css; charset=utf-8";
    if (absl::EndsWith(path, ".json") || absl::EndsWith(path, ".map"))
        return "application/json; charset=utf-8";
    if (absl::EndsWith(path, ".svg")) return "image/svg+xml";
    if (absl::EndsWith(path, ".png")) return "image/png";
    if (absl::EndsWith(path, ".jpg") || absl::EndsWith(path, ".jpeg")) return "image/jpeg";
    if (absl::EndsWith(path, ".ico")) return "image/x-icon";
    if (absl::EndsWith(path, ".wasm")) return "application/wasm";
    return "application/octet-stream";
}

fs::path LocateStaticDir(const std::string& custom_dir) {
    if (!custom_dir.empty() && fs::exists(custom_dir)) {
        return fs::canonical(custom_dir);
    }

    // 1. Resolve via Bazel Runfiles when running under Bazel
    if (android::base::Bazel::InBazel()) {
        for (const char* candidate_prefix : {
                 "goldfish+/emulator/ui/aquarium/dist",
                 "goldfish/emulator/ui/aquarium/dist",
                 "android_emulator/hardware/generic/goldfish/emulator/ui/aquarium/dist",
                 "_main/hardware/generic/goldfish/emulator/ui/aquarium/dist",
             }) {
            std::string runfiles_path = android::base::Bazel::RunfilesPath(candidate_prefix);
            if (fs::exists(runfiles_path) && fs::exists(fs::path(runfiles_path) / "index.html")) {
                return fs::canonical(runfiles_path);
            }
        }
    }

    // 2. Development & fallback lookup candidates
    std::vector<fs::path> candidates;

    const char* bwd = std::getenv("BUILD_WORKING_DIRECTORY");
    if (bwd && *bwd) {
        fs::path ws(bwd);
        candidates.push_back(ws / "hardware/generic/goldfish/emulator/ui/aquarium/dist");
        candidates.push_back(ws / "emulator/ui/aquarium/dist");
        candidates.push_back(ws / "hardware/generic/goldfish/emulator/ui/aquarium");
        candidates.push_back(ws / "emulator/ui/aquarium");
    }

    candidates.push_back("hardware/generic/goldfish/emulator/ui/aquarium/dist");
    candidates.push_back("emulator/ui/aquarium/dist");
    candidates.push_back("../emulator/ui/aquarium/dist");
    candidates.push_back("hardware/generic/goldfish/emulator/ui/aquarium");
    candidates.push_back("emulator/ui/aquarium");
    candidates.push_back("../emulator/ui/aquarium");

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::exists(candidate / "index.html")) {
            return fs::canonical(candidate);
        }
    }

    if (bwd && *bwd) {
        return fs::path(bwd) / "hardware/generic/goldfish/emulator/ui/aquarium/dist";
    }
    return "hardware/generic/goldfish/emulator/ui/aquarium/dist";
}

goldfish::http::HttpResponse ServeStaticFile(const fs::path& base_dir, std::string_view req_path) {
    std::error_code ec;
    fs::path canonical_base = fs::canonical(base_dir, ec);
    if (ec) {
        canonical_base = base_dir;
    }

    std::string rel_path(req_path);
    if (rel_path == "/" || rel_path.empty()) {
        rel_path = "index.html";
    } else if (rel_path.front() == '/') {
        rel_path = rel_path.substr(1);
    }

    fs::path file_path = canonical_base / rel_path;
    file_path = fs::canonical(file_path, ec);

    // Prevent path traversal
    if (ec || !absl::StartsWith(file_path.string(), canonical_base.string()) ||
        !fs::exists(file_path) || fs::is_directory(file_path)) {
        // Missing static assets with extensions should return 404 rather than
        // masking the error by falling back to index.html with MIME text/html.
        std::string extension = fs::path(rel_path).extension().string();
        if (!extension.empty() && extension != ".html" && extension != ".htm") {
            return goldfish::http::HttpResponse::String("404 Not Found",
                                                        goldfish::http::HttpStatus::kNotFound);
        }

        // Fallback to index.html for SPA client-side routing
        file_path = canonical_base / "index.html";
        if (!fs::exists(file_path)) {
            return goldfish::http::HttpResponse::String("404 Not Found",
                                                        goldfish::http::HttpStatus::kNotFound);
        }
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return goldfish::http::HttpResponse::String(
                "500 Internal Server Error", goldfish::http::HttpStatus::kInternalServerError);
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    std::string_view mime_type = GetMimeType(file_path.string());
    return goldfish::http::HttpResponse::String(std::move(content), goldfish::http::HttpStatus::kOk,
                                                mime_type)
            .WithHeader("cache-control", "no-cache");
}

absl::StatusOr<std::pair<std::string, std::shared_ptr<grpc::Channel>>> ResolveGrpcTargetAndChannel(
        const std::string& target, const std::string& token, const std::string& discovery_file) {
    EmulatorGrpcClientBuilder builder;
    if (!target.empty()) {
        Endpoint endpoint;
        endpoint.set_target(target);
        if (!token.empty()) {
            auto* header = endpoint.add_required_headers();
            header->set_key("authorization");
            header->set_value("Bearer " + token);
        }
        builder.WithEndpoint(endpoint);
    } else if (!discovery_file.empty()) {
        builder.WithDiscoveryFile(discovery_file);
    } else {
        builder.ForDiscoveredEmulator({});
    }

    auto client = builder.BuildBlocking();
    if (!client.ok()) {
        return client.status();
    }

    const auto& endpoint = (*client)->GetEndpoint();
    std::string resolved_target = endpoint.target();
    GrpcChannelFactory factory(endpoint, {});
    auto channel = factory.CreateChannel();
    if (!channel) {
        return absl::InternalError("Failed to create gRPC channel for target: " + resolved_target);
    }
    return std::make_pair(std::move(resolved_target), std::move(channel));
}

}  // namespace goldfish::aquarium
