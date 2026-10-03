// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

// Client side of the netsim packet stream, for hosts such as the emulator.
//
// One PacketTransport carries the packets of one chip. The API mirrors
// goldfish's NetsimTransport (initialize/send/next_recv) so that emulator call
// sites switch over mechanically, and it exposes no protobuf or gRPC types.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"

namespace goldfish::netsim {

enum class ChipKind { kBluetooth, kWifi, kUwb, kNfc, kCellular, kCellularData, kEthernet };

// Describes the device that owns the chip (ChipInfo.device_info over gRPC).
struct DeviceInfo {
    std::string name;
    std::string kind = "EMULATOR";
    std::string version;
    std::string sdk_version;
    std::string build_id;
    std::string variant;
    std::string arch;
    std::string avd_path;
};

// Describes the chip (ChipInfo.chip over gRPC). Only `kind` is required.
struct ChipConfig {
    ChipKind kind = ChipKind::kBluetooth;
    std::string id;
    std::string manufacturer;
    std::string product_name;
    std::string address;
    // CELLULAR only: ICC profile XML contents.
    std::optional<std::string> sim_profile;
    // netsimd echoes every packet back instead of attaching a chip. For
    // transport tests and benchmarks.
    bool loopback = false;
};

struct TransportOptions {
    DeviceInfo device;
    std::string server_address;
    uint16_t instance = 1;
};

// One packet. BLUETOOTH: H4 (packet type byte followed by the HCI packet).
// Other kinds: the raw payload, as carried in PacketRequest.packet.
using Packet = std::vector<uint8_t>;

class PacketTransport {
  public:
    // Runs on a transport thread. The callee may move the packet out. Return
    // true to receive the next packet, or false to pause until next_recv().
    using RecvCallback = std::function<bool(Packet& packet)>;

    // Closes the stream and blocks until no callback is running or will run.
    // Must not be called from the RecvCallback.
    virtual ~PacketTransport() = default;

    // Connects, announces the chip and device, and starts receiving.
    virtual absl::Status initialize(const ChipConfig& chip) = 0;

    // Queues a packet for sending. Thread-safe; never blocks on the network.
    // Packets sent after the stream has closed are dropped.
    virtual void send(Packet packet) = 0;

    // Resumes receiving after the RecvCallback returned false. Thread-safe.
    virtual void next_recv() = 0;
};

// Factory function to create a PacketTransport instance.
// Currently returns the default gRPC-based transport implementation.
std::unique_ptr<PacketTransport> CreatePacketTransport(PacketTransport::RecvCallback recv_cb);

}  // namespace goldfish::netsim
