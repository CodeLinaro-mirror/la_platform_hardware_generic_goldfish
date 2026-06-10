// Copyright 2026 The Android Open Source Project
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

#include "android/crashreport/breadcrumb_proto.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

#include "android/status/status_matcher_macros.h"

namespace android::crashreport {

TEST(BreadcrumbProtoTest, LogValidBreadcrumb) {
    auto status = LogBreadcrumb(BreadcrumbType::kGrpc, 12345, BreadcrumbPhase::kInstant,
                                PayloadType::kGrpcProto, "test payload");
    EXPECT_OK(status);
}

TEST(BreadcrumbProtoTest, LogInvalidBreadcrumbType) {
    auto invalid_type = static_cast<BreadcrumbType>(999);
    auto status = LogBreadcrumb(invalid_type, 12345, BreadcrumbPhase::kInstant,
                                PayloadType::kGrpcProto, "test payload");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(), "Invalid breadcrumb type: 999");
}

}  // namespace android::crashreport
