// Copyright (C) 2024 The Android Open Source Project
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
#include <grpcpp/support/status.h>

#include "absl/status/status.h"

namespace android::emulation::control {
/**
 * @brief Translates an absl::Status to a grpc::Status.
 *
 * This function maps an `absl::Status` object to its corresponding
 * `grpc::Status` representation. This allows for interoperability between
 * code using the Abseil status library and gRPC.  The function maps common
 * `absl::StatusCode` values to their `grpc::StatusCode` equivalents. If no
 * direct mapping exists, the function defaults to
 * `grpc::StatusCode::UNKNOWN`. The error message from the absl::Status is
 * always preserved.
 *
 * @param absl_status The input `absl::Status` object to be translated.
 * @return The equivalent `grpc::Status` object. If the input status is OK,
 *         returns a grpc::Status::OK.
 */
grpc::Status AbslStatusToGrpcStatus(const absl::Status& absl_status);

/**
 * @brief Translates a grpc::Status to an absl::Status.
 *
 * This function maps a `grpc::Status` object to its corresponding
 * `absl::Status` representation. This allows for interoperability between
 * code using the Abseil status library and gRPC. The function maps common
 * `grpc::StatusCode` values to their `absl::StatusCode` equivalents. If no
 * direct mapping exists, the function defaults to
 * `absl::StatusCode::kUnknown`. The error message from the grpc::Status is
 * always preserved.
 *
 * @param grpc_status The input `grpc::Status` object to be translated.
 * @return The equivalent `absl::Status` object. If the input status is OK,
 *         returns an absl::OkStatus().
 */
absl::Status GrpcStatusToAbslStatus(const grpc::Status& grpc_status);
}  // namespace android::emulation::control