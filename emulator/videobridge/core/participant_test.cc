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

// Lifecycle and JSEP state machine coverage for Participant.
//
// A participant is driven by whatever a signaling client sends, in whatever
// order it sends it, including after the session has been torn down. These
// tests exercise that ordering directly rather than through Switchboard, so
// that the interleavings a client can actually produce are reachable.
//
// Everything runs on an in-memory network: no port is bound and no host
// interface is enumerated.

#include "participant.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "nlohmann/json.hpp"
#include "virtual_network_fixture.h"

namespace goldfish::videobridge {
namespace {

/**
 * @brief A minimal RtcConnection that records what a participant sends it.
 *
 * RtcConnection has only three pure virtuals, so a real participant can be
 * hosted by a double this small. Signaling output lands in a vector instead of
 * a queue, which is what makes the assertions below direct.
 */
class RecordingConnection : public RtcConnection {
  public:
    RecordingConnection() : RtcConnection(VirtualSwitchboard::MakeVirtualSubstrate()) {}

    void Send(std::string to, const nlohmann::json& msg) override {
        const absl::MutexLock lock(&mutex_);
        sent_.push_back(msg);
    }

    void RtcConnectionClosed(std::string participant) override {
        const absl::MutexLock lock(&mutex_);
        ++closed_notifications_;
    }

    std::unique_ptr<InputSender> CreateInputSender(DataChannelLabel /*label*/) override {
        return nullptr;
    }

    // Blocks until a sent message carries `key`, or the budget expires. The
    // budget bounds failure only; a passing run returns as soon as the message
    // arrives, so there is no fixed cost and no sleep.
    nlohmann::json AwaitMessageWith(const std::string& key) {
        const auto has_match = [this, &key]() {
            mutex_.AssertReaderHeld();
            for (const auto& msg : sent_) {
                if (msg.contains(key)) {
                    return true;
                }
            }
            return false;
        };
        const absl::MutexLock lock(&mutex_);
        if (!mutex_.AwaitWithTimeout(absl::Condition(&has_match), absl::Seconds(10))) {
            return nlohmann::json::object();
        }
        for (const auto& msg : sent_) {
            if (msg.contains(key)) {
                return msg;
            }
        }
        return nlohmann::json::object();
    }

    int SentCount() {
        const absl::MutexLock lock(&mutex_);
        return static_cast<int>(sent_.size());
    }

    int ClosedNotifications() {
        const absl::MutexLock lock(&mutex_);
        return closed_notifications_;
    }

  private:
    absl::Mutex mutex_;
    std::vector<nlohmann::json> sent_ ABSL_GUARDED_BY(mutex_);
    int closed_notifications_ ABSL_GUARDED_BY(mutex_) = 0;
};

// Participant calls shared_from_this() while handling offers and candidates,
// so it can only ever be held by a shared_ptr. A stack instance aborts with
// std::bad_weak_ptr on the first message, which is a confusing way to find out.
std::shared_ptr<Participant> MakeParticipant(RecordingConnection& connection) {
    auto participant =
            std::make_shared<Participant>(connection, "peer", nlohmann::json::object(), nullptr);
    EXPECT_TRUE(participant->Initialize().ok());
    return participant;
}

nlohmann::json Offer() {
    return nlohmann::json{{"type", "offer"}, {"sdp", kDataChannelOffer}};
}

// A syntactically valid candidate for the m-line the offer above declares.
nlohmann::json Candidate() {
    return nlohmann::json{{"sdpMid", "0"},
                          {"sdpMLineIndex", 0},
                          {"candidate",
                           "candidate:1 1 udp 2122260223 192.168.13.37 49152 typ host "
                           "generation 0 ufrag tOQd network-id 1"}};
}

// ---------------------------------------------------------------------------
// Baseline: the happy path, so that the negative tests below mean something.
// ---------------------------------------------------------------------------

TEST(ParticipantTest, AnswersAnOffer) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->IncomingMessage(Offer());

    const nlohmann::json answer = connection.AwaitMessageWith("type");
    ASSERT_FALSE(answer.empty()) << "No answer was produced for the offer.";
    EXPECT_EQ(answer["type"], "answer");

    participant->Close();
}

// ---------------------------------------------------------------------------
// Teardown ordering.
//
// Switchboard::Disconnect removes a participant from the connection map and
// then closes it, but AcceptJsepMessage has already taken its own shared_ptr
// by then. A client that sends a message while disconnecting therefore gets a
// message delivered to a closed participant. Both calls funnel onto the
// signaling thread, so whichever arrives second runs against the state the
// first one left behind.
// ---------------------------------------------------------------------------

TEST(ParticipantTest, IgnoresAnOfferReceivedAfterClose) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->Close();
    participant->IncomingMessage(Offer());

    // Nothing to assert beyond survival: the message must be dropped rather
    // than run against the torn-down PeerConnection.
    SUCCEED();
}

TEST(ParticipantTest, IgnoresACandidateReceivedAfterClose) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    // Negotiate first, so that the remote description is set and the candidate
    // path is past the point where it merely buffers.
    participant->IncomingMessage(Offer());
    ASSERT_FALSE(connection.AwaitMessageWith("type").empty());

    participant->Close();
    participant->IncomingMessage(Candidate());

    SUCCEED();
}

TEST(ParticipantTest, IgnoresMessagesArrivingAfterCloseDuringNegotiation) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->IncomingMessage(Offer());
    participant->Close();

    // A client that keeps trickling after it has gone away.
    participant->IncomingMessage(Candidate());
    participant->IncomingMessage(Offer());
    participant->IncomingMessage(Candidate());

    SUCCEED();
}

TEST(ParticipantTest, CloseIsIdempotent) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);
    participant->IncomingMessage(Offer());
    ASSERT_FALSE(connection.AwaitMessageWith("type").empty());

    participant->Close();
    participant->Close();
    participant->Close();

    // Closing an already closed participant must not re-notify the owner, or
    // Switchboard would reclaim a session id that may have been reused.
    EXPECT_EQ(connection.ClosedNotifications(), 1);
}

TEST(ParticipantTest, CloseBeforeAnyNegotiationIsSafe) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->Close();

    EXPECT_EQ(connection.SentCount(), 0);
}

// ---------------------------------------------------------------------------
// Candidate ordering.
//
// Trickle ICE lets candidates arrive before the description they belong to, so
// the participant buffers them until the remote description is set. If that
// buffering broke, the candidates would be handed to a PeerConnection that has
// no m-line to attach them to and negotiation would not complete.
// ---------------------------------------------------------------------------

TEST(ParticipantTest, AcceptsCandidatesThatArriveBeforeTheOffer) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->IncomingMessage(Candidate());
    participant->IncomingMessage(Candidate());
    participant->IncomingMessage(Offer());

    const nlohmann::json answer = connection.AwaitMessageWith("type");
    EXPECT_FALSE(answer.empty()) << "Early candidates prevented the offer from being answered.";

    participant->Close();
}

// ---------------------------------------------------------------------------
// Malformed input, at the participant level rather than the parser level.
// ---------------------------------------------------------------------------

TEST(ParticipantTest, SurvivesMalformedMessages) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    for (const char* raw : {
             R"({})",
             R"({"sdp": {"type": 42, "sdp": "v=0"}})",
             R"({"candidate": {"sdpMid": 0, "sdpMLineIndex": 0, "candidate": "c"}})",
             R"({"candidate": {"sdpMid": "0", "sdpMLineIndex": 0, "candidate": "garbage"}})",
             R"({"sdp": {"type": "answer", "sdp": "not sdp"}})",
         }) {
        participant->IncomingMessage(nlohmann::json::parse(raw));
    }

    // Still able to negotiate afterwards.
    participant->IncomingMessage(Offer());
    EXPECT_FALSE(connection.AwaitMessageWith("type").empty());

    participant->Close();
}

// An answer with no preceding offer is a protocol violation, not a crash.
TEST(ParticipantTest, SurvivesAnAnswerWithoutAnOffer) {
    RecordingConnection connection;
    auto participant = MakeParticipant(connection);

    participant->IncomingMessage(nlohmann::json{{"type", "answer"}, {"sdp", kDataChannelOffer}});

    participant->Close();
}

}  // namespace
}  // namespace goldfish::videobridge
