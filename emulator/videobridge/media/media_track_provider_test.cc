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

#include "goldfish/videobridge/media_track_provider.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/environment/environment_factory.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "p2p/client/basic_port_allocator.h"
#pragma clang diagnostic pop

#include "goldfish/async/testing/global_event_loop.h"
#include "goldfish/display/display.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/videobridge/in_process_audio_source.h"
#include "goldfish/videobridge/in_process_video_source.h"
#include "goldfish/videobridge/switchboard.h"

namespace goldfish::videobridge {
namespace {

class DummyPeerConnectionObserver : public ::webrtc::PeerConnectionObserver {
  public:
    void OnSignalingChange(::webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<::webrtc::DataChannelInterface>) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceGatheringChange(::webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const ::webrtc::IceCandidateInterface*) override {}
};

class MediaTrackProviderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        board_ = std::make_unique<Switchboard>(nullptr);
        board_->SignalingThread()->BlockingCall([this] {
            auto allocator = std::make_unique<::webrtc::BasicPortAllocator>(
                    ::webrtc::CreateEnvironment(), board_->GetNetworkManager(),
                    board_->GetSocketFactory());
            ::webrtc::PeerConnectionDependencies deps(&observer_);
            deps.allocator = std::move(allocator);
            auto res = board_->GetPeerConnectionFactory()->CreatePeerConnectionOrError(
                    ::webrtc::PeerConnectionInterface::RTCConfiguration(), std::move(deps));
            ASSERT_TRUE(res.ok()) << res.error().message();
            peer_connection_ = res.MoveValue();
        });
    }

    void TearDown() override {
        board_->SignalingThread()->BlockingCall([this] {
            if (peer_connection_) {
                peer_connection_->Close();
                peer_connection_ = nullptr;
            }
        });
        board_ = nullptr;
    }

    DummyPeerConnectionObserver observer_;
    std::unique_ptr<Switchboard> board_;
    webrtc::scoped_refptr<::webrtc::PeerConnectionInterface> peer_connection_;
};

TEST_F(MediaTrackProviderTest, NullFactoryOrPeerConnectionReturnsInvalidArgument) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    MediaTrackProvider provider(video_source);

    EXPECT_EQ(provider.AddTracks(nullptr, nullptr).code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(provider.AddTracks(board_->GetPeerConnectionFactory(), nullptr).code(),
              absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(provider.AddTracks(nullptr, peer_connection_.get()).code(),
              absl::StatusCode::kInvalidArgument);
}

TEST_F(MediaTrackProviderTest, NeitherVideoNorAudioConfiguredReturnsFailedPrecondition) {
    MediaTrackProvider provider(nullptr, nullptr);

    board_->SignalingThread()->BlockingCall([&] {
        auto status =
                provider.AddTracks(board_->GetPeerConnectionFactory(), peer_connection_.get());
        EXPECT_FALSE(status.ok());
        EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
    });
}

TEST_F(MediaTrackProviderTest, GettersReturnConfiguredSources) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    auto audio_source = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
    MediaTrackProvider provider(video_source, audio_source);

    EXPECT_EQ(provider.video_source(), video_source);
    EXPECT_EQ(provider.audio_source(), audio_source);
}

TEST_F(MediaTrackProviderTest, AttachesBothVideoAndAudioTracks) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    auto audio_source = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
    MediaTrackProvider provider(video_source, audio_source);

    board_->SignalingThread()->BlockingCall([&] {
        auto status =
                provider.AddTracks(board_->GetPeerConnectionFactory(), peer_connection_.get());
        EXPECT_TRUE(status.ok()) << status;

        // WebRTC peer connection should have 2 active senders (video + audio)
        EXPECT_EQ(peer_connection_->GetSenders().size(), 2U);
    });
}

TEST_F(MediaTrackProviderTest, AttachesVideoOnlyTrack) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    MediaTrackProvider provider(video_source, nullptr);

    board_->SignalingThread()->BlockingCall([&] {
        auto status =
                provider.AddTracks(board_->GetPeerConnectionFactory(), peer_connection_.get());
        EXPECT_TRUE(status.ok()) << status;

        EXPECT_EQ(peer_connection_->GetSenders().size(), 1U);
    });
}

TEST_F(MediaTrackProviderTest, AttachesAudioOnlyTrack) {
    auto audio_source = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
    MediaTrackProvider provider(nullptr, audio_source);

    board_->SignalingThread()->BlockingCall([&] {
        auto status =
                provider.AddTracks(board_->GetPeerConnectionFactory(), peer_connection_.get());
        EXPECT_TRUE(status.ok()) << status;

        EXPECT_EQ(peer_connection_->GetSenders().size(), 1U);
    });
}

TEST_F(MediaTrackProviderTest, CustomTrackAndStreamIdentifiers) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 1);
    auto audio_source = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
    MediaTrackProvider provider(video_source, audio_source, "custom_vtrack", "custom_vstream",
                                "custom_atrack", "custom_astream");

    board_->SignalingThread()->BlockingCall([&] {
        auto status =
                provider.AddTracks(board_->GetPeerConnectionFactory(), peer_connection_.get());
        EXPECT_TRUE(status.ok()) << status;

        auto senders = peer_connection_->GetSenders();
        ASSERT_EQ(senders.size(), 2U);

        bool found_custom_video = false;
        bool found_custom_audio = false;
        for (const auto& sender : senders) {
            auto track = sender->track();
            if (track && track->id() == "custom_vtrack") {
                found_custom_video = true;
                auto stream_ids = sender->stream_ids();
                ASSERT_EQ(stream_ids.size(), 1U);
                EXPECT_EQ(stream_ids[0], "custom_vstream");
            }
            if (track && track->id() == "custom_atrack") {
                found_custom_audio = true;
                auto stream_ids = sender->stream_ids();
                ASSERT_EQ(stream_ids.size(), 1U);
                EXPECT_EQ(stream_ids[0], "custom_astream");
            }
        }
        EXPECT_TRUE(found_custom_video);
        EXPECT_TRUE(found_custom_audio);
    });
}

TEST(MediaTrackProviderSwitchboardTest, ConnectAndDisconnectParticipantEndToEnd) {
    goldfish::display::test::FakeMultiDisplay fake_multidisplay(goldfish::async::globalEventLoop());
    auto video_source = ::webrtc::make_ref_counted<InProcessVideoSource>(fake_multidisplay, 0);
    auto audio_source = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
    auto provider = std::make_shared<MediaTrackProvider>(video_source, audio_source);

    Switchboard board(provider);
    EXPECT_TRUE(board.Connect("participant1"));
    board.Disconnect("participant1");
}

}  // namespace
}  // namespace goldfish::videobridge
