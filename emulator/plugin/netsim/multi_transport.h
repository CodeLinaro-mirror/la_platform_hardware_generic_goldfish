// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file multi_transport.h
 * @brief Runtime packet transport selector (kAuto, kGrpc, kWebSocket) for the
 *        netsim packet stream.
 */

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "emulator/plugin/netsim/packet_transport.h"

namespace goldfish::netsim {

/**
 * @brief Runtime packet transport selection mode for the Netsim packet stream.
 */
enum class TransportType {
    /// Tries WebSocket first; if that fails with NOT_FOUND, UNAVAILABLE, or
    /// UNIMPLEMENTED, falls back to gRPC. Other errors are returned as is.
    /// The choice is made once, in initialize(), before any packet is sent.
    kAuto,
    /// Uses the gRPC transport exclusively.
    kGrpc,
    /// Uses the WebSocket transport exclusively.
    kWebSocket,
};

/// Legacy environment variable for selecting the Netsim packet transport.
inline constexpr char kTransportEnvVar[] = "NETSIM_TRANSPORT";

/// Primary environment variable for selecting the Netsim packet stream transport.
inline constexpr char kPacketStreamEnvVar[] = "NETSIM_PACKETSTREAM";

/**
 * @brief Parses a transport selection string ("auto", "grpc", or "websocket",
 *        case-insensitive).
 * @param value Raw configuration value to parse.
 * @return Parsed TransportType, or InvalidArgumentError if unrecognized.
 */
absl::StatusOr<TransportType> ParseTransportType(std::string_view value);

/**
 * @brief Reads NETSIM_PACKETSTREAM or NETSIM_TRANSPORT from the process
 *        environment. Unset or empty selects TransportType::kAuto.
 * @return Resolved TransportType, or InvalidArgumentError if set to an invalid
 *         value.
 */
absl::StatusOr<TransportType> TransportTypeFromEnv();

/**
 * @brief Runtime transport adapter that delegates to WebSocket or gRPC based on
 *        TransportType, with automatic fallback in TransportType::kAuto mode.
 */
class MultiTransport : public PacketTransport {
  public:
    /// Factory callback constructing an underlying PacketTransport instance.
    using TransportFactory =
            std::function<std::unique_ptr<PacketTransport>(PacketTransport::RecvCallback)>;

    /**
     * @brief Constructs a MultiTransport runtime selector.
     * @param type Desired transport selection mode.
     * @param recv_cb Callback invoked when packets arrive from the active transport.
     * @param grpc_factory Factory used to create the gRPC transport.
     * @param ws_factory Factory used to create the WebSocket transport.
     */
    MultiTransport(TransportType type, RecvCallback recv_cb, TransportFactory grpc_factory,
                   TransportFactory ws_factory);
    ~MultiTransport() override;

    /**
     * @brief Initializes the selected underlying transport for the given chip.
     * @param chip Per-chip radio configuration.
     * @return OkStatus on success, or an error status explaining why initialization failed.
     */
    absl::Status initialize(const ChipConfig& chip) override;

    /**
     * @brief Forwards an outbound packet to the active transport, if initialized.
     * @param packet Raw packet bytes to transmit.
     */
    void send(Packet packet) override;

    /**
     * @brief Signals readiness for the next inbound packet on the active transport.
     */
    void next_recv() override;

  private:
    absl::Status InitializeWithFactory(const TransportFactory& factory, std::string_view name,
                                       const ChipConfig& chip);
    std::shared_ptr<PacketTransport> Use(std::unique_ptr<PacketTransport> transport);
    std::shared_ptr<PacketTransport> active();

    const TransportType type_;
    const RecvCallback recv_cb_;
    const TransportFactory grpc_factory_;
    const TransportFactory ws_factory_;
    absl::Mutex mu_;
    bool initialized_ ABSL_GUARDED_BY(mu_) = false;
    std::shared_ptr<PacketTransport> active_ ABSL_GUARDED_BY(mu_);
};

}  // namespace goldfish::netsim
