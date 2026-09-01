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
#pragma once

#include <grpcpp/grpcpp.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"

#include "goldfish/http/http_response.h"

namespace goldfish::aquarium {

// Returns the MIME content-type corresponding to the given file path extension.
std::string_view GetMimeType(std::string_view path);

// Resolves the path to the directory containing Aquarium static web assets.
// Checks custom_dir, Bazel runfiles, and local development candidate directories.
std::filesystem::path LocateStaticDir(const std::string& custom_dir = "");

// Serves a static file from base_dir or falls back to index.html for SPA routing.
goldfish::http::HttpResponse ServeStaticFile(const std::filesystem::path& base_dir,
                                             std::string_view req_path);

// Resolves upstream gRPC endpoint and creates an active gRPC channel.
absl::StatusOr<std::pair<std::string, std::shared_ptr<grpc::Channel>>> ResolveGrpcTargetAndChannel(
        const std::string& target, const std::string& token, const std::string& discovery_file);

}  // namespace goldfish::aquarium
