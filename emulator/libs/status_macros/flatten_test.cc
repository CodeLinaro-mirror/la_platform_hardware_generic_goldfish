/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "android/status/flatten.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"

namespace android::status {
namespace {

// Compile-time static assertions for type transformations.
static_assert(std::is_same_v<flatten_status_or_t<void>, absl::Status>);
static_assert(std::is_same_v<flatten_status_or_t<absl::Status>, absl::Status>);
static_assert(std::is_same_v<flatten_status_or_t<const absl::Status>, absl::Status>);
static_assert(std::is_same_v<flatten_status_or_t<absl::Status&>, absl::Status>);
static_assert(std::is_same_v<flatten_status_or_t<const absl::Status&>, absl::Status>);
static_assert(std::is_same_v<flatten_status_or_t<absl::Status&&>, absl::Status>);

static_assert(std::is_same_v<flatten_status_or_t<int>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<const int>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<int&>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<const int&>, absl::StatusOr<int>>);

static_assert(std::is_same_v<flatten_status_or_t<absl::StatusOr<int>>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<const absl::StatusOr<int>>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<absl::StatusOr<int>&>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_status_or_t<const absl::StatusOr<int>&>, absl::StatusOr<int>>);

static_assert(std::is_same_v<flatten_status_or_t<std::string>, absl::StatusOr<std::string>>);
static_assert(std::is_same_v<flatten_status_or_t<absl::StatusOr<std::string>>,
                             absl::StatusOr<std::string>>);

// Alias check
static_assert(std::is_same_v<flatten_t<void>, absl::Status>);
static_assert(std::is_same_v<flatten_t<absl::Status>, absl::Status>);
static_assert(std::is_same_v<flatten_t<int>, absl::StatusOr<int>>);
static_assert(std::is_same_v<flatten_t<absl::StatusOr<int>>, absl::StatusOr<int>>);

template <typename F>
auto InvokeAndFlatten(F&& fn) -> flatten_status_or_t<std::invoke_result_t<std::decay_t<F>>> {
    using R = std::invoke_result_t<std::decay_t<F>>;
    if constexpr (std::is_void_v<R>) {
        std::forward<F>(fn)();
        return absl::OkStatus();
    } else if constexpr (std::is_same_v<std::decay_t<R>, absl::Status>) {
        return std::forward<F>(fn)();
    } else {
        return std::forward<F>(fn)();
    }
}

TEST(FlattenTest, VoidReturnsStatus) {
    bool executed = false;
    auto res = InvokeAndFlatten([&executed] { executed = true; });
    static_assert(std::is_same_v<decltype(res), absl::Status>);
    EXPECT_TRUE(executed);
    EXPECT_THAT(res, absl_testing::IsOk());
}

TEST(FlattenTest, StatusReturnsStatus) {
    auto ok_res = InvokeAndFlatten([]() -> absl::Status { return absl::OkStatus(); });
    static_assert(std::is_same_v<decltype(ok_res), absl::Status>);
    EXPECT_THAT(ok_res, absl_testing::IsOk());

    auto err_res = InvokeAndFlatten(
            []() -> absl::Status { return absl::InvalidArgumentError("bad argument"); });
    static_assert(std::is_same_v<decltype(err_res), absl::Status>);
    EXPECT_THAT(err_res, absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(FlattenTest, StatusOrReturnsStatusOr) {
    auto ok_res = InvokeAndFlatten([]() -> absl::StatusOr<int> { return 42; });
    static_assert(std::is_same_v<decltype(ok_res), absl::StatusOr<int>>);
    EXPECT_THAT(ok_res, absl_testing::IsOkAndHolds(42));

    auto err_res = InvokeAndFlatten(
            []() -> absl::StatusOr<int> { return absl::NotFoundError("not found"); });
    static_assert(std::is_same_v<decltype(err_res), absl::StatusOr<int>>);
    EXPECT_THAT(err_res, absl_testing::StatusIs(absl::StatusCode::kNotFound));
}

TEST(FlattenTest, PlainValueReturnsStatusOr) {
    auto res = InvokeAndFlatten([]() -> int { return 123; });
    static_assert(std::is_same_v<decltype(res), absl::StatusOr<int>>);
    EXPECT_THAT(res, absl_testing::IsOkAndHolds(123));
}

}  // namespace
}  // namespace android::status
