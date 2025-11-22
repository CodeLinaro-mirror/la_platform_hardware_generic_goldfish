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
#include "android/grpc/utils/absl_status_translate.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace android::emulation::control {
TEST(AbslStatusTranslateTest, OkStatus) {
    absl::Status abslStatus = absl::OkStatus();
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_TRUE(grpcStatus.ok());
}

TEST(AbslStatusTranslateTest, CancelledStatus) {
    absl::Status abslStatus = absl::CancelledError();
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::CANCELLED);
}

TEST(AbslStatusTranslateTest, InvalidArgumentStatus) {
    absl::Status abslStatus = absl::InvalidArgumentError("invalid argument");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_EQ(grpcStatus.error_message(), "invalid argument");
}

TEST(AbslStatusTranslateTest, DeadlineExceededStatus) {
    absl::Status abslStatus = absl::DeadlineExceededError("deadline exceeded");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
    EXPECT_EQ(grpcStatus.error_message(), "deadline exceeded");
}

TEST(AbslStatusTranslateTest, NotFoundStatus) {
    absl::Status abslStatus = absl::NotFoundError("not found");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_EQ(grpcStatus.error_message(), "not found");
}

TEST(AbslStatusTranslateTest, AlreadyExistsStatus) {
    absl::Status abslStatus = absl::AlreadyExistsError("already exists");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::ALREADY_EXISTS);
    EXPECT_EQ(grpcStatus.error_message(), "already exists");
}

TEST(AbslStatusTranslateTest, PermissionDeniedStatus) {
    absl::Status abslStatus = absl::PermissionDeniedError("permission denied");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_EQ(grpcStatus.error_message(), "permission denied");
}

TEST(AbslStatusTranslateTest, ResourceExhaustedStatus) {
    absl::Status abslStatus = absl::ResourceExhaustedError("resource exhausted");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::RESOURCE_EXHAUSTED);
    EXPECT_EQ(grpcStatus.error_message(), "resource exhausted");
}

TEST(AbslStatusTranslateTest, FailedPreconditionStatus) {
    absl::Status abslStatus = absl::FailedPreconditionError("failed precondition");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::FAILED_PRECONDITION);
    EXPECT_EQ(grpcStatus.error_message(), "failed precondition");
}

TEST(AbslStatusTranslateTest, AbortedStatus) {
    absl::Status abslStatus = absl::AbortedError("aborted");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::ABORTED);
    EXPECT_EQ(grpcStatus.error_message(), "aborted");
}

TEST(AbslStatusTranslateTest, OutOfRangeStatus) {
    absl::Status abslStatus = absl::OutOfRangeError("out of range");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::OUT_OF_RANGE);
    EXPECT_EQ(grpcStatus.error_message(), "out of range");
}

TEST(AbslStatusTranslateTest, UnimplementedStatus) {
    absl::Status abslStatus = absl::UnimplementedError("unimplemented");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::UNIMPLEMENTED);
    EXPECT_EQ(grpcStatus.error_message(), "unimplemented");
}

TEST(AbslStatusTranslateTest, InternalStatus) {
    absl::Status abslStatus = absl::InternalError("internal");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_EQ(grpcStatus.error_message(), "internal");
}

TEST(AbslStatusTranslateTest, UnavailableStatus) {
    absl::Status abslStatus = absl::UnavailableError("unavailable");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::UNAVAILABLE);
    EXPECT_EQ(grpcStatus.error_message(), "unavailable");
}

TEST(AbslStatusTranslateTest, DataLossStatus) {
    absl::Status abslStatus = absl::DataLossError("data loss");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::DATA_LOSS);
    EXPECT_EQ(grpcStatus.error_message(), "data loss");
}

TEST(AbslStatusTranslateTest, UnauthenticatedStatus) {
    absl::Status abslStatus = absl::UnauthenticatedError("unauthenticated");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    EXPECT_EQ(grpcStatus.error_message(), "unauthenticated");
}

TEST(AbslStatusTranslateTest, UnknownStatus) {
    absl::Status abslStatus = absl::UnknownError("unknown");
    grpc::Status grpcStatus = abslStatusToGrpcStatus(abslStatus);
    EXPECT_EQ(grpcStatus.error_code(), grpc::StatusCode::UNKNOWN);
    EXPECT_EQ(grpcStatus.error_message(), "unknown");
}
}  // namespace android::emulation::control