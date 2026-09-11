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

// Exercises a real participant negotiating over an entirely in-memory network.
//
// Nothing here touches the host: no port is bound, and no interface is
// enumerated. The single address the participant can gather on is one this
// test invents, which is what makes the assertions below meaningful -- a
// candidate carrying that address cannot have come from anywhere else.

#include <gtest/gtest.h>

#include <string>

#include "absl/strings/match.h"

#include "nlohmann/json.hpp"
#include "virtual_network_fixture.h"

namespace goldfish::videobridge {
namespace {

TEST(VirtualNetworkTest, AnswersAnOfferWithoutTouchingTheHost) {
    VirtualSwitchboard board;
    ASSERT_TRUE(board.Connect("peer", "{}"));

    const nlohmann::json offer = {{"type", "offer"}, {"sdp", kDataChannelOffer}};
    ASSERT_TRUE(board.AcceptJsepMessage("peer", offer.dump()).ok());

    const nlohmann::json answer = AwaitMessage(
            board, "peer", [](const nlohmann::json& msg) { return msg.contains("type"); });

    ASSERT_FALSE(answer.empty()) << "No answer was produced for the offer.";
    EXPECT_EQ(answer["type"], "answer");
    EXPECT_FALSE(answer["sdp"].get<std::string>().empty());

    board.Disconnect("peer");
}

TEST(VirtualNetworkTest, GathersCandidatesOnlyOnTheInventedInterface) {
    VirtualSwitchboard board;
    ASSERT_TRUE(board.Connect("peer", "{}"));

    const nlohmann::json offer = {{"type", "offer"}, {"sdp", kDataChannelOffer}};
    ASSERT_TRUE(board.AcceptJsepMessage("peer", offer.dump()).ok());

    const nlohmann::json candidate = AwaitMessage(
            board, "peer", [](const nlohmann::json& msg) { return msg.contains("candidate"); });

    ASSERT_FALSE(candidate.empty()) << "No ICE candidate was gathered.";
    const std::string line = candidate["candidate"]["candidate"].get<std::string>();

    // The decisive assertion. This address belongs to no real adapter, so a
    // candidate carrying it proves gathering went through the substituted
    // network rather than the machine running the test.
    EXPECT_TRUE(absl::StrContains(line, kFakeInterface))
            << "Candidate did not come from the virtual interface: " << line;

    board.Disconnect("peer");
}

}  // namespace
}  // namespace goldfish::videobridge
