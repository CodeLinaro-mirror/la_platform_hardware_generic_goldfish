// Copyright (C) 2023 The Android Open Source Project
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

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace android::emulation::control {
TEST(AbslStatusTranslateTest, OkStatus) {
    const absl::Status absl_status = absl::OkStatus();
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_TRUE(grpc_status.ok());
}

TEST(AbslStatusTranslateTest, CancelledStatus) {
    const absl::Status absl_status = absl::CancelledError();
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST(AbslStatusTranslateTest, InvalidArgumentStatus) {
    const absl::Status absl_status = absl::InvalidArgumentError("invalid argument");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(grpc_status.error_message(), "invalid argument");
}

TEST(AbslStatusTranslateTest, DeadlineExceededStatus) {
    const absl::Status absl_status = absl::DeadlineExceededError("deadline exceeded");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
    EXPECT_EQ(grpc_status.error_message(), "deadline exceeded");
}

TEST(AbslStatusTranslateTest, NotFoundStatus) {
    const absl::Status absl_status = absl::NotFoundError("not found");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_EQ(grpc_status.error_message(), "not found");
}

TEST(AbslStatusTranslateTest, AlreadyExistsStatus) {
    const absl::Status absl_status = absl::AlreadyExistsError("already exists");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::ALREADY_EXISTS);
    EXPECT_EQ(grpc_status.error_message(), "already exists");
}

TEST(AbslStatusTranslateTest, PermissionDeniedStatus) {
    const absl::Status absl_status = absl::PermissionDeniedError("permission denied");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_EQ(grpc_status.error_message(), "permission denied");
}

TEST(AbslStatusTranslateTest, ResourceExhaustedStatus) {
    const absl::Status absl_status = absl::ResourceExhaustedError("resource exhausted");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::RESOURCE_EXHAUSTED);
    EXPECT_EQ(grpc_status.error_message(), "resource exhausted");
}

TEST(AbslStatusTranslateTest, FailedPreconditionStatus) {
    const absl::Status absl_status = absl::FailedPreconditionError("failed precondition");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::FAILED_PRECONDITION);
    EXPECT_EQ(grpc_status.error_message(), "failed precondition");
}

TEST(AbslStatusTranslateTest, AbortedStatus) {
    const absl::Status absl_status = absl::AbortedError("aborted");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::ABORTED);
    EXPECT_EQ(grpc_status.error_message(), "aborted");
}

TEST(AbslStatusTranslateTest, OutOfRangeStatus) {
    const absl::Status absl_status = absl::OutOfRangeError("out of range");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::OUT_OF_RANGE);
    EXPECT_EQ(grpc_status.error_message(), "out of range");
}

TEST(AbslStatusTranslateTest, UnimplementedStatus) {
    const absl::Status absl_status = absl::UnimplementedError("unimplemented");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::UNIMPLEMENTED);
    EXPECT_EQ(grpc_status.error_message(), "unimplemented");
}

TEST(AbslStatusTranslateTest, InternalStatus) {
    const absl::Status absl_status = absl::InternalError("internal");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_EQ(grpc_status.error_message(), "internal");
}

TEST(AbslStatusTranslateTest, UnavailableStatus) {
    const absl::Status absl_status = absl::UnavailableError("unavailable");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::UNAVAILABLE);
    EXPECT_EQ(grpc_status.error_message(), "unavailable");
}

TEST(AbslStatusTranslateTest, DataLossStatus) {
    const absl::Status absl_status = absl::DataLossError("data loss");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::DATA_LOSS);
    EXPECT_EQ(grpc_status.error_message(), "data loss");
}

TEST(AbslStatusTranslateTest, UnauthenticatedStatus) {
    const absl::Status absl_status = absl::UnauthenticatedError("unauthenticated");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    EXPECT_EQ(grpc_status.error_message(), "unauthenticated");
}

TEST(AbslStatusTranslateTest, UnknownStatus) {
    const absl::Status absl_status = absl::UnknownError("unknown");
    const grpc::Status grpc_status = AbslStatusToGrpcStatus(absl_status);
    EXPECT_EQ(grpc_status.error_code(), grpc::StatusCode::UNKNOWN);
    EXPECT_EQ(grpc_status.error_message(), "unknown");
}
}  // namespace android::emulation::control