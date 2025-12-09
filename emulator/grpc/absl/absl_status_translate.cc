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
#include "android/emulation/control/absl_status_translate.h"

namespace android::emulation::control {
grpc::Status abslStatusToGrpcStatus(const absl::Status& absl_status) {
    if (absl_status.ok()) {
        return grpc::Status::OK;
    }

    grpc::StatusCode grpc_code = grpc::StatusCode::UNKNOWN;  // Default

    // Map some common absl::StatusCodes to grpc::StatusCodes.  Expand as needed.
    switch (absl_status.code()) {
    case absl::StatusCode::kCancelled:
        grpc_code = grpc::StatusCode::CANCELLED;
        break;
    case absl::StatusCode::kInvalidArgument:
        grpc_code = grpc::StatusCode::INVALID_ARGUMENT;
        break;
    case absl::StatusCode::kDeadlineExceeded:
        grpc_code = grpc::StatusCode::DEADLINE_EXCEEDED;
        break;
    case absl::StatusCode::kNotFound:
        grpc_code = grpc::StatusCode::NOT_FOUND;
        break;
    case absl::StatusCode::kAlreadyExists:
        grpc_code = grpc::StatusCode::ALREADY_EXISTS;
        break;
    case absl::StatusCode::kPermissionDenied:
        grpc_code = grpc::StatusCode::PERMISSION_DENIED;
        break;
    case absl::StatusCode::kResourceExhausted:
        grpc_code = grpc::StatusCode::RESOURCE_EXHAUSTED;
        break;
    case absl::StatusCode::kFailedPrecondition:
        grpc_code = grpc::StatusCode::FAILED_PRECONDITION;
        break;
    case absl::StatusCode::kAborted:
        grpc_code = grpc::StatusCode::ABORTED;
        break;
    case absl::StatusCode::kOutOfRange:
        grpc_code = grpc::StatusCode::OUT_OF_RANGE;
        break;
    case absl::StatusCode::kUnimplemented:
        grpc_code = grpc::StatusCode::UNIMPLEMENTED;
        break;
    case absl::StatusCode::kInternal:
        grpc_code = grpc::StatusCode::INTERNAL;
        break;
    case absl::StatusCode::kUnavailable:
        grpc_code = grpc::StatusCode::UNAVAILABLE;
        break;
    case absl::StatusCode::kDataLoss:
        grpc_code = grpc::StatusCode::DATA_LOSS;
        break;
    case absl::StatusCode::kUnauthenticated:
        grpc_code = grpc::StatusCode::UNAUTHENTICATED;
        break;
        // Add more mappings as needed...
    }

    return ::grpc::Status(grpc_code, std::string(absl_status.message()));
}
}  // namespace android::emulation::control