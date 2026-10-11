// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/multi_transport.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"

#include "android/base/testing/test_system.h"

namespace goldfish::netsim {
namespace {

struct FakeTransportState {
    absl::Status init_status = absl::OkStatus();
    bool initialized = false;
    ChipConfig chip;
    std::vector<Packet> sent_packets;
    int next_recv_count = 0;
    PacketTransport::RecvCallback recv_cb;
    std::function<void(PacketTransport::RecvCallback&)> on_init;
};

class FakeTransport : public PacketTransport {
  public:
    FakeTransport(std::shared_ptr<FakeTransportState> state, RecvCallback recv_cb)
            : state_(std::move(state)), recv_cb_(std::move(recv_cb)) {
        state_->recv_cb = recv_cb_;
    }

    absl::Status initialize(const ChipConfig& chip) override {
        state_->initialized = true;
        state_->chip = chip;
        if (state_->on_init) {
            state_->on_init(recv_cb_);
        }
        return state_->init_status;
    }

    void send(Packet packet) override { state_->sent_packets.push_back(std::move(packet)); }

    void next_recv() override { ++state_->next_recv_count; }

  private:
    std::shared_ptr<FakeTransportState> state_;
    RecvCallback recv_cb_;
};

MultiTransport::TransportFactory MakeFactory(std::shared_ptr<FakeTransportState> state) {
    return [state = std::move(state)](PacketTransport::RecvCallback recv_cb) {
        return std::make_unique<FakeTransport>(state, std::move(recv_cb));
    };
}

TEST(TransportTypeTest, ParsesValues) {
    EXPECT_EQ(*ParseTransportType("auto"), TransportType::kAuto);
    EXPECT_EQ(*ParseTransportType("grpc"), TransportType::kGrpc);
    EXPECT_EQ(*ParseTransportType("WebSocket"), TransportType::kWebSocket);
    EXPECT_FALSE(ParseTransportType("tcp").ok());
    EXPECT_FALSE(ParseTransportType("").ok());
}

TEST(TransportTypeTest, EnvOverridesDefault) {
    android::base::TestSystem test_system("/progdir");
    EXPECT_EQ(*TransportTypeFromEnv(), TransportType::kAuto);

    test_system.EnvSet(kTransportEnvVar, "websocket");
    EXPECT_EQ(*TransportTypeFromEnv(), TransportType::kWebSocket);
    test_system.EnvSet(kTransportEnvVar, "grpc");
    EXPECT_EQ(*TransportTypeFromEnv(), TransportType::kGrpc);
    test_system.EnvSet(kTransportEnvVar, "auto");
    EXPECT_EQ(*TransportTypeFromEnv(), TransportType::kAuto);

    // NETSIM_PACKETSTREAM takes precedence over NETSIM_TRANSPORT.
    test_system.EnvSet(kPacketStreamEnvVar, "websocket");
    test_system.EnvSet(kTransportEnvVar, "grpc");
    EXPECT_EQ(*TransportTypeFromEnv(), TransportType::kWebSocket);

    test_system.EnvSet(kPacketStreamEnvVar, "bogus");
    absl::StatusOr<TransportType> bad = TransportTypeFromEnv();
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.status().message().find(kPacketStreamEnvVar), std::string_view::npos);
}

TEST(MultiTransportTest, EmptyRecvCallbackFailsWithInvalidArgument) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    MultiTransport transport(TransportType::kGrpc, PacketTransport::RecvCallback{},
                             MakeFactory(grpc_state), MakeFactory(ws_state));

    absl::Status status = transport.initialize({.kind = ChipKind::kBluetooth});
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_FALSE(grpc_state->initialized);
    EXPECT_FALSE(ws_state->initialized);
}

TEST(MultiTransportTest, NullFactoryOrNullTransportReturnsUnimplementedAndAutoFallsBack) {
    auto grpc_state = std::make_shared<FakeTransportState>();

    MultiTransport unconfigured_ws(
            TransportType::kWebSocket, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MultiTransport::TransportFactory{});
    EXPECT_EQ(unconfigured_ws.initialize({.kind = ChipKind::kWifi}).code(),
              absl::StatusCode::kUnimplemented);

    MultiTransport null_returning_grpc(
            TransportType::kGrpc, [](Packet&) { return true; },
            [](PacketTransport::RecvCallback) -> std::unique_ptr<PacketTransport> {
                return nullptr;
            },
            MultiTransport::TransportFactory{});
    EXPECT_EQ(null_returning_grpc.initialize({.kind = ChipKind::kBluetooth}).code(),
              absl::StatusCode::kUnimplemented);

    // In kAuto mode, a null WebSocket factory falls back to gRPC.
    MultiTransport auto_fallback(
            TransportType::kAuto, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MultiTransport::TransportFactory{});
    EXPECT_TRUE(auto_fallback.initialize({.kind = ChipKind::kBluetooth}).ok());
    EXPECT_TRUE(grpc_state->initialized);
}

TEST(MultiTransportTest, GrpcModeUsesGrpcDirectly) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    std::vector<Packet> received;
    MultiTransport transport(
            TransportType::kGrpc,
            [&](Packet& p) {
                received.push_back(p);
                return true;
            },
            MakeFactory(grpc_state), MakeFactory(ws_state));

    EXPECT_TRUE(transport.initialize({.kind = ChipKind::kBluetooth}).ok());
    EXPECT_TRUE(grpc_state->initialized);
    EXPECT_FALSE(ws_state->initialized);

    transport.send({0x01, 0x02});
    ASSERT_EQ(grpc_state->sent_packets.size(), 1u);
    EXPECT_EQ(grpc_state->sent_packets[0], (Packet{0x01, 0x02}));

    transport.next_recv();
    EXPECT_EQ(grpc_state->next_recv_count, 1);
}

TEST(MultiTransportTest, WebSocketModeUsesWebSocketDirectly) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    MultiTransport transport(
            TransportType::kWebSocket, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    EXPECT_TRUE(transport.initialize({.kind = ChipKind::kWifi}).ok());
    EXPECT_TRUE(ws_state->initialized);
    EXPECT_FALSE(grpc_state->initialized);

    transport.send({0xAA});
    ASSERT_EQ(ws_state->sent_packets.size(), 1u);
    EXPECT_EQ(ws_state->sent_packets[0], (Packet{0xAA}));
}

TEST(MultiTransportTest, AutoPrefersWebSocketWhenAvailable) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    MultiTransport transport(
            TransportType::kAuto, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    EXPECT_TRUE(transport.initialize({.kind = ChipKind::kUwb}).ok());
    EXPECT_TRUE(ws_state->initialized);
    EXPECT_FALSE(grpc_state->initialized);

    transport.send({0x10});
    EXPECT_EQ(ws_state->sent_packets.size(), 1u);
    EXPECT_TRUE(grpc_state->sent_packets.empty());
}

TEST(MultiTransportTest, AutoFallsBackToGrpcOnRecoverableErrors) {
    for (absl::StatusCode code : {absl::StatusCode::kUnavailable, absl::StatusCode::kNotFound,
                                  absl::StatusCode::kUnimplemented}) {
        auto grpc_state = std::make_shared<FakeTransportState>();
        auto ws_state = std::make_shared<FakeTransportState>();
        ws_state->init_status = absl::Status(code, "ws unavailable");

        MultiTransport transport(
                TransportType::kAuto, [](Packet&) { return true; }, MakeFactory(grpc_state),
                MakeFactory(ws_state));

        EXPECT_TRUE(transport.initialize({.kind = ChipKind::kNfc}).ok());
        EXPECT_TRUE(ws_state->initialized);
        EXPECT_TRUE(grpc_state->initialized);

        transport.send({0x20});
        EXPECT_TRUE(ws_state->sent_packets.empty());
        ASSERT_EQ(grpc_state->sent_packets.size(), 1u);
        EXPECT_EQ(grpc_state->sent_packets[0], (Packet{0x20}));
    }
}

TEST(MultiTransportTest, AutoDoesNotFallBackOnNonRecoverableError) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();
    ws_state->init_status = absl::InvalidArgumentError("bad config");

    MultiTransport transport(
            TransportType::kAuto, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    absl::Status status = transport.initialize({.kind = ChipKind::kBluetooth});
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(ws_state->initialized);
    EXPECT_FALSE(grpc_state->initialized);
}

TEST(MultiTransportTest, AutoReportsBothErrorsWhenFallbackFails) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();
    ws_state->init_status = absl::UnavailableError("ws refused");
    grpc_state->init_status = absl::UnavailableError("grpc refused");

    MultiTransport transport(
            TransportType::kAuto, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    absl::Status status = transport.initialize({.kind = ChipKind::kBluetooth});
    EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
    EXPECT_NE(status.message().find("grpc refused"), std::string_view::npos);
    EXPECT_NE(status.message().find("ws refused"), std::string_view::npos);
}

TEST(MultiTransportTest, NextRecvDuringInitializeReachesActiveTransport) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    MultiTransport* transport_ptr = nullptr;
    ws_state->on_init = [&](PacketTransport::RecvCallback& cb) {
        Packet pkt = {0x99};
        cb(pkt);
    };

    MultiTransport transport(
            TransportType::kAuto,
            [&](Packet&) {
                transport_ptr->next_recv();
                return false;
            },
            MakeFactory(grpc_state), MakeFactory(ws_state));
    transport_ptr = &transport;

    EXPECT_TRUE(transport.initialize({.kind = ChipKind::kBluetooth}).ok());
    EXPECT_EQ(ws_state->next_recv_count, 1);
}

TEST(MultiTransportTest, SecondInitializeFails) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();

    MultiTransport transport(
            TransportType::kGrpc, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    EXPECT_TRUE(transport.initialize({}).ok());
    EXPECT_EQ(transport.initialize({}).code(), absl::StatusCode::kFailedPrecondition);
}

TEST(MultiTransportTest, InitializeCanRetryAfterFailure) {
    auto grpc_state = std::make_shared<FakeTransportState>();
    auto ws_state = std::make_shared<FakeTransportState>();
    grpc_state->init_status = absl::UnavailableError("transient failure");

    MultiTransport transport(
            TransportType::kGrpc, [](Packet&) { return true; }, MakeFactory(grpc_state),
            MakeFactory(ws_state));

    EXPECT_EQ(transport.initialize({}).code(), absl::StatusCode::kUnavailable);

    grpc_state->init_status = absl::OkStatus();
    EXPECT_TRUE(transport.initialize({}).ok());
    EXPECT_EQ(transport.initialize({}).code(), absl::StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace goldfish::netsim
