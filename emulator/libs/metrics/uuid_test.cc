/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/metrics/uuid.h"

#include <gtest/gtest.h>

#include "android/status/status_matcher_macros.h"

namespace goldfish::metrics {

TEST(UuidTest, Zero) {
    Uuid uuid = Uuid::Zero();
    EXPECT_EQ(uuid.ToString(), "00000000-0000-0000-0000-000000000000");
}

TEST(UuidTest, Generate) {
    auto uuid1 = Uuid::Generate();
    auto uuid2 = Uuid::Generate();
    EXPECT_NE(uuid1, Uuid::Zero());
    EXPECT_NE(uuid2, Uuid::Zero());
    EXPECT_NE(uuid1, uuid2);
}

TEST(UuidTest, ParseValid) {
    std::string s = "550e8400-e29b-41d4-a716-446655440000";
    ASSERT_OK_AND_ASSIGN(auto uuid, Uuid::FromString(s));
    EXPECT_EQ(uuid.ToString(), s);
}

TEST(UuidTest, ParseInvalid) {
    EXPECT_THAT(Uuid::FromString("not-a-uuid"),
                ::absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UuidTest, Equality) {
    Uuid uuid1 = Uuid::Generate();
    ASSERT_OK_AND_ASSIGN(auto uuid2, Uuid::FromString(uuid1.ToString()));
    EXPECT_EQ(uuid1, uuid2);
}

}  // namespace goldfish::metrics
