// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "packet_transport.h"

#include <gtest/gtest.h>

#include <queue>

namespace goldfish::netsim {
namespace {

// Test implementation of PacketTransport for interface verification.
class TestPacketTransport : public PacketTransport {
  public:
    explicit TestPacketTransport(RecvCallback recv_cb) : recv_cb_(std::move(recv_cb)) {}

    absl::Status initialize(const ChipConfig& chip) override {
        chip_ = chip;
        initialized_ = true;
        return absl::OkStatus();
    }

    void send(Packet packet) override { sent_packets_.push(std::move(packet)); }

    void next_recv() override { paused_ = false; }

    void deliver(Packet packet) {
        if (!recv_cb_(packet)) {
            paused_ = true;
        }
    }

    const ChipConfig& chip() const { return chip_; }
    bool initialized() const { return initialized_; }
    bool paused() const { return paused_; }
    std::queue<Packet>& sent_packets() { return sent_packets_; }

  private:
    RecvCallback recv_cb_;
    ChipConfig chip_;
    bool initialized_ = false;
    bool paused_ = false;
    std::queue<Packet> sent_packets_;
};

TEST(PacketTransportTest, InitializeAndSend) {
    TestPacketTransport transport([](Packet&) { return true; });
    ChipConfig chip{
        .kind = ChipKind::kBluetooth,
        .address = "00:11:22:33:44:55",
    };
    EXPECT_TRUE(transport.initialize(chip).ok());
    EXPECT_TRUE(transport.initialized());
    EXPECT_EQ(transport.chip().kind, ChipKind::kBluetooth);
    EXPECT_EQ(transport.chip().address, "00:11:22:33:44:55");

    Packet packet = {0x01, 0x02, 0x03};
    transport.send(packet);
    ASSERT_EQ(transport.sent_packets().size(), 1u);
    EXPECT_EQ(transport.sent_packets().front(), packet);
}

TEST(PacketTransportTest, FlowControlPauseAndResume) {
    std::vector<Packet> received;
    TestPacketTransport transport([&](Packet& p) {
        received.push_back(p);
        return false;  // Request pause (backpressure).
    });

    EXPECT_TRUE(transport.initialize({.kind = ChipKind::kWifi}).ok());
    Packet p1 = {0xAA, 0xBB};
    transport.deliver(p1);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], p1);
    EXPECT_TRUE(transport.paused());

    transport.next_recv();
    EXPECT_FALSE(transport.paused());
}

}  // namespace
}  // namespace goldfish::netsim
