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
grpc::Status AbslStatusToGrpcStatus(const absl::Status& absl_status) {
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
    case absl::StatusCode::kOk:
        grpc_code = grpc::StatusCode::OK;
        break;
    case absl::StatusCode::kUnknown:
    default:
        grpc_code = grpc::StatusCode::UNKNOWN;
        break;
    }

    return {grpc_code, std::string(absl_status.message())};
}
absl::Status GrpcStatusToAbslStatus(const grpc::Status& grpc_status) {
    if (grpc_status.ok()) {
        return absl::OkStatus();
    }

    absl::StatusCode absl_code = absl::StatusCode::kUnknown;  // Default

    switch (grpc_status.error_code()) {
    case grpc::StatusCode::OK:
        absl_code = absl::StatusCode::kOk;
        break;
    case grpc::StatusCode::CANCELLED:
        absl_code = absl::StatusCode::kCancelled;
        break;
    case grpc::StatusCode::INVALID_ARGUMENT:
        absl_code = absl::StatusCode::kInvalidArgument;
        break;
    case grpc::StatusCode::DEADLINE_EXCEEDED:
        absl_code = absl::StatusCode::kDeadlineExceeded;
        break;
    case grpc::StatusCode::NOT_FOUND:
        absl_code = absl::StatusCode::kNotFound;
        break;
    case grpc::StatusCode::ALREADY_EXISTS:
        absl_code = absl::StatusCode::kAlreadyExists;
        break;
    case grpc::StatusCode::PERMISSION_DENIED:
        absl_code = absl::StatusCode::kPermissionDenied;
        break;
    case grpc::StatusCode::RESOURCE_EXHAUSTED:
        absl_code = absl::StatusCode::kResourceExhausted;
        break;
    case grpc::StatusCode::FAILED_PRECONDITION:
        absl_code = absl::StatusCode::kFailedPrecondition;
        break;
    case grpc::StatusCode::ABORTED:
        absl_code = absl::StatusCode::kAborted;
        break;
    case grpc::StatusCode::OUT_OF_RANGE:
        absl_code = absl::StatusCode::kOutOfRange;
        break;
    case grpc::StatusCode::UNIMPLEMENTED:
        absl_code = absl::StatusCode::kUnimplemented;
        break;
    case grpc::StatusCode::INTERNAL:
        absl_code = absl::StatusCode::kInternal;
        break;
    case grpc::StatusCode::UNAVAILABLE:
        absl_code = absl::StatusCode::kUnavailable;
        break;
    case grpc::StatusCode::DATA_LOSS:
        absl_code = absl::StatusCode::kDataLoss;
        break;
    case grpc::StatusCode::UNAUTHENTICATED:
        absl_code = absl::StatusCode::kUnauthenticated;
        break;
    case grpc::StatusCode::UNKNOWN:
    default:
        absl_code = absl::StatusCode::kUnknown;
        break;
    }

    return absl::Status(absl_code, grpc_status.error_message());
}
}  // namespace android::emulation::control
