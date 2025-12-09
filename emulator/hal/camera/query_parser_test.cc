/* Copyright 2025 The Android Open Source Project
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

#include "goldfish/devices/camera/query_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

using namespace std::literals;
using namespace goldfish::devices::camera;

using ::testing::ElementsAre;
using ::testing::Pair;

TEST(QueryParser, zero) {
    constexpr std::string_view kZero = "\0"sv;

    std::vector<std::pair<std::string, std::string>> results;

    const auto sink = [&results](std::string_view query, std::string_view params) {
        results.push_back(std::make_pair(std::string(query), std::string(params)));
    };

    QueryParser qp;

    qp.recv(kZero.data(), kZero.size(), sink);
    EXPECT_TRUE(results.empty());
    qp.recv(kZero.data(), kZero.size(), sink);
    EXPECT_TRUE(results.empty());
}

TEST(QueryParser, merge1) {
    constexpr std::string_view kCat = "cat"sv;
    constexpr std::string_view kSpace = " "sv;
    constexpr std::string_view kVideo = "video"sv;
    constexpr std::string_view kS = "s"sv;
    constexpr std::string_view kZeroAndSome = "\0some"sv;

    std::vector<std::pair<std::string, std::string>> results;

    const auto sink = [&results](std::string_view query, std::string_view params) {
        results.push_back(std::make_pair(std::string(query), std::string(params)));
    };

    QueryParser qp;

    qp.recv(kCat.data(), kCat.size(), sink);
    EXPECT_TRUE(results.empty());
    qp.recv(kSpace.data(), kSpace.size(), sink);
    EXPECT_TRUE(results.empty());
    qp.recv(kVideo.data(), kVideo.size(), sink);
    EXPECT_TRUE(results.empty());
    qp.recv(kS.data(), kS.size(), sink);
    EXPECT_TRUE(results.empty());
    qp.recv(kZeroAndSome.data(), kZeroAndSome.size(), sink);
    EXPECT_THAT(results, ElementsAre(Pair("cat"s, "videos"s)));
}

TEST(QueryParser, multiple) {
    std::vector<std::pair<std::string, std::string>> results;

    const auto sink = [&results](std::string_view query, std::string_view params) {
        results.push_back(std::make_pair(std::string(query), std::string(params)));
    };

    constexpr std::string_view kText = "cat videos\0raccoon\0"sv;

    QueryParser qp;
    qp.recv(kText.data(), kText.size(), sink);

    EXPECT_THAT(results, ElementsAre(Pair("cat"s, "videos"s), Pair("raccoon"s, ""s)));
}
