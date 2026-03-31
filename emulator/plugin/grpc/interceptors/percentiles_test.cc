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

#include "android/control/interceptor/percentiles.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <random>
#include <vector>

namespace android::control::interceptor {

using namespace testing;

static const int kNumSamples = 10000;
static const int kRawDataSize = 80;

static std::vector<double> GetNormalDistribution(int count, double offset = 0) {
    std::vector<double> res;
    res.reserve(count);

    static std::mt19937 rand_engine;
    std::normal_distribution<double> r;
    for (int i = 0; i < count; ++i) {
        res.push_back(r(rand_engine) + offset);
    }

    return res;
}

TEST(PercentilesTest, Basic) {
    auto p = Percentiles(kRawDataSize, {0.5});
    EXPECT_EQ(1, p.TargetCount());
    EXPECT_EQ(0.5, p.Target(0));
    EXPECT_FALSE(p.CalcValueForTargetAtIndex(0));
    EXPECT_FALSE(p.CalcValueForTarget(0.5));
}

TEST(PercentilesTest, Targets) {
    auto p = Percentiles(kRawDataSize, {0.25, 0.5, 0.75});
    EXPECT_EQ(3, p.TargetCount());
    EXPECT_FALSE(p.Target(-1));
    EXPECT_EQ(0.25, p.Target(0));
    EXPECT_EQ(0.5, p.Target(1));
    EXPECT_EQ(0.75, p.Target(2));
    EXPECT_FALSE(p.Target(3));

    EXPECT_FALSE(p.CalcValueForTargetAtIndex(0));

    p.AddSample(1);
    EXPECT_FALSE(p.CalcValueForTarget(0.2));
    EXPECT_DOUBLE_EQ(1, p.CalcValueForTarget(0.25).value_or(0));
    EXPECT_DOUBLE_EQ(1, p.CalcValueForTarget(0.50).value_or(0));
    EXPECT_DOUBLE_EQ(1, p.CalcValueForTarget(0.75).value_or(0));
    EXPECT_FALSE(p.CalcValueForTarget(0.9));

    p.AddSamples({2.0, 3.0, 4.0, 5.0, 6.0});
    EXPECT_FALSE(p.CalcValueForTarget(0.2));
    EXPECT_DOUBLE_EQ(2, p.CalcValueForTarget(0.25).value_or(0));
    EXPECT_DOUBLE_EQ(4, p.CalcValueForTarget(0.50).value_or(0));
    EXPECT_DOUBLE_EQ(5, p.CalcValueForTarget(0.75).value_or(0));
    EXPECT_FALSE(p.CalcValueForTarget(0.9));
}

TEST(PercentilesTest, ConstantDistribution) {
    auto p = Percentiles(kRawDataSize, {0.5});
    for (int i = 0; i < kNumSamples; ++i) {
        p.AddSample(1.0);
    }

    EXPECT_NEAR(1.0, p.CalcValueForTarget(0.5).value_or(0), 0.01);
    EXPECT_NEAR(1.0, p.CalcValueForTargetAtIndex(0).value_or(0), 0.01);
}

TEST(PercentilesTest, UniformDistribution) {
    std::uniform_real_distribution<double> r(0.0, 1.0);
    static std::mt19937 rand_engine;

    auto p = Percentiles(kRawDataSize, {0.25, 0.5, 0.75});
    for (int i = 0; i < kNumSamples; ++i) {
        p.AddSample(r(rand_engine));
    }

    EXPECT_NEAR(0.25, p.CalcValueForTarget(0.25).value_or(0), 0.01);
    EXPECT_NEAR(0.5, p.CalcValueForTarget(0.5).value_or(0), 0.01);
    EXPECT_NEAR(0.75, p.CalcValueForTarget(0.75).value_or(0), 0.01);
    EXPECT_NEAR(0.25, p.CalcValueForTargetAtIndex(0).value_or(0), 0.01);
    EXPECT_NEAR(0.5, p.CalcValueForTargetAtIndex(1).value_or(0), 0.01);
    EXPECT_NEAR(0.75, p.CalcValueForTargetAtIndex(2).value_or(0), 0.01);
}

TEST(PercentilesTest, BimodalDistributionTest) {
    std::uniform_real_distribution<double> r(0.0, 1.0);
    static std::mt19937 rand_engine;

    auto p = Percentiles(kRawDataSize, {0.5});
    for (int i = 0; i < kNumSamples; ++i) {
        double d = r(rand_engine);
        double v;
        if (d < 0.45) {
            v = 1.0;
        } else if (d < 0.55) {
            v = 2.0;
        } else {
            v = 3.0;
        }
        p.AddSample(v);
    }

    EXPECT_NEAR(2.0, p.CalcValueForTarget(0.5).value_or(0), 0.05);
    EXPECT_NEAR(2.0, p.CalcValueForTargetAtIndex(0).value_or(0), 0.05);
}

TEST(PercentilesTest, NormalDistributionTest) {
    auto p = Percentiles(kRawDataSize, {0.5});
    p.AddSamples(GetNormalDistribution(kNumSamples, 6.0));
    EXPECT_NEAR(6.0, p.CalcValueForTarget(0.5).value_or(0), 0.03);
    EXPECT_NEAR(6.0, p.CalcValueForTargetAtIndex(0).value_or(0), 0.03);
}

TEST(PercentilesTest, BucketizeReport) {
    auto p = Percentiles(kRawDataSize, {0.5});
    for (int i = 0; i < kRawDataSize; i++) {
        p.AddSample(i);
        EXPECT_EQ(i + 1, p.SamplesCount());
        EXPECT_FALSE(p.IsBucketized());
    }
    p.AddSample(1);
    EXPECT_TRUE(p.IsBucketized());
}

TEST(PercentilesTest, OutOfBounds) {
    auto p = Percentiles(kRawDataSize, {0.5});

    // Test Target() out of bounds
    EXPECT_FALSE(p.Target(-1).has_value());
    EXPECT_FALSE(p.Target(1).has_value());

    // Test CalcValueForTargetAtIndex() out of bounds
    EXPECT_FALSE(p.CalcValueForTargetAtIndex(-1).has_value());
    EXPECT_FALSE(p.CalcValueForTargetAtIndex(1).has_value());
}

TEST(PercentilesTest, NormalMultiplePointsTest) {
    auto p = Percentiles(kRawDataSize, {0.4, 0.5, 0.6});
    auto entries = GetNormalDistribution(kNumSamples, 6.0);
    p.AddSamples(entries);

    std::sort(entries.begin(), entries.end());
    const double actual40th = entries[static_cast<size_t>(0.4 * entries.size())];
    const double actual50th = entries[static_cast<size_t>(0.5 * entries.size())];
    const double actual60th = entries[static_cast<size_t>(0.6 * entries.size())];

    EXPECT_NEAR(actual40th, p.CalcValueForTarget(0.4).value_or(actual40th - 100), 0.03);
    EXPECT_NEAR(actual50th, p.CalcValueForTarget(0.5).value_or(actual50th - 100), 0.03);
    EXPECT_NEAR(actual60th, p.CalcValueForTarget(0.6).value_or(actual60th - 100), 0.03);
}

}  // namespace android::control::interceptor
