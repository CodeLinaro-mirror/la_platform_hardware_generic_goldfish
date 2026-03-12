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
#include "goldfish/parsing/parameter_binder.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

namespace goldfish::parsing {

class ParameterBinderTest : public ::testing::Test {
  protected:
    struct DummyContext {};
    DummyContext ctx_;
};

TEST_F(ParameterBinderTest, FunctionTraitsLambda) {
    auto lambda = [](DummyContext&, int, double) { return std::string("ok"); };
    using Traits = FunctionTraits<decltype(lambda)>;

    EXPECT_EQ(Traits::kArity, 2);
    static_assert(std::is_same_v<typename Traits::args_tuple, std::tuple<int, double>>);
}

TEST_F(ParameterBinderTest, InvokeFromStreamSuccess) {
    auto func = [](DummyContext&, int a, double b) {
        return "sum: " + std::to_string(a + static_cast<int>(b));
    };
    using Traits = FunctionTraits<decltype(func)>;

    ArgStream args("10 5.5");
    auto result = InvokeFromStream(func, ctx_, args, std::make_index_sequence<Traits::kArity>{});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "sum: 15");
}

TEST_F(ParameterBinderTest, InvokeFromStreamNormalization) {
    // 1. absl::Status return
    {
        auto f = [](DummyContext&, int) { return absl::OkStatus(); };
        using T = FunctionTraits<decltype(f)>;
        ArgStream args("1");
        auto res = InvokeFromStream(f, ctx_, args, std::make_index_sequence<T::kArity>{});
        ASSERT_TRUE(res.ok());
        EXPECT_EQ(*res, "");
    }

    // 2. std::string return
    {
        auto f = [](DummyContext&, int v) { return std::to_string(v); };
        using T = FunctionTraits<decltype(f)>;
        ArgStream args("42");
        auto res = InvokeFromStream(f, ctx_, args, std::make_index_sequence<T::kArity>{});
        ASSERT_TRUE(res.ok());
        EXPECT_EQ(*res, "42");
    }

    // 3. const char* nullptr return
    {
        auto f = [](DummyContext&, int) -> const char* { return nullptr; };
        using T = FunctionTraits<decltype(f)>;
        ArgStream args("1");
        auto res = InvokeFromStream(f, ctx_, args, std::make_index_sequence<T::kArity>{});
        ASSERT_TRUE(res.ok());
        EXPECT_EQ(*res, "");
    }
}

TEST_F(ParameterBinderTest, InvokeFromStreamTooManyArguments) {
    auto func = [](DummyContext&, int) { return std::string("ok"); };
    using Traits = FunctionTraits<decltype(func)>;

    ArgStream args("10 20");
    auto result = InvokeFromStream(func, ctx_, args, std::make_index_sequence<Traits::kArity>{});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(), "Too many arguments");
}

TEST_F(ParameterBinderTest, InvokeFromStreamParsingError) {
    auto func = [](DummyContext&, int, int) { return std::string("ok"); };
    using Traits = FunctionTraits<decltype(func)>;

    ArgStream args("10 not_an_int");
    auto result = InvokeFromStream(func, ctx_, args, std::make_index_sequence<Traits::kArity>{});

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ParameterBinderTest, InvokeFromStreamEvaluationOrder) {
    auto func = [](DummyContext&, std::string first, std::string second) { return first + second; };
    using Traits = FunctionTraits<decltype(func)>;

    ArgStream args("A B");
    auto result = InvokeFromStream(func, ctx_, args, std::make_index_sequence<Traits::kArity>{});

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "AB");
}

TEST_F(ParameterBinderTest, BindAndInvokeWrapper) {
    auto func = [](DummyContext&, int a, std::string b) { return std::to_string(a) + ":" + b; };

    ArgStream args("42 hello");
    auto result = BindAndInvoke(func, ctx_, args);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "42:hello");
}

TEST_F(ParameterBinderTest, RValueOnlyFunctor) {
    struct RValueOnly {
        absl::StatusOr<std::string> operator()(DummyContext&, int val) && {
            return std::to_string(val);
        }
    };

    // Make sure we do perfect forwarding
    ArgStream args("123");
    auto result = BindAndInvoke(RValueOnly{}, ctx_, args);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "123");
}

}  // namespace goldfish::parsing
