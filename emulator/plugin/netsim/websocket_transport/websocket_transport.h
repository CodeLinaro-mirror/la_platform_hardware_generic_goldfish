// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file websocket_transport.h
 * @brief WebSocket client implementation of PacketTransport for Netsim packet
 *        streams.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "emulator/plugin/netsim/packet_transport.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"

namespace goldfish::netsim {

/**
 * @brief Returns the URL path segment for a radio chip kind (`"bt"`, `"wifi"`,
 *        `"uwb"`, `"nfc"`, `"cellular"`, `"cellular_data"`, or `"ethernet"`).
 * @param kind Radio chip kind.
 * @return URL path segment string view.
 */
std::string_view WebSocketKindName(ChipKind kind);

/**
 * @brief Formats the WebSocket request path (`"/v1/websocket/{kind}?..."`)
 *        encoding initial device and chip metadata as URL query parameters.
 * @param chip Per-chip radio configuration.
 * @param options Transport options containing device metadata.
 * @return Request path and query string (omitting empty fields).
 */
std::string WebSocketPath(const ChipConfig& chip, const TransportOptions& options);

/**
 * @brief Creates a WebSocket `PacketTransport` driven by the caller-provided
 *        `EventLoop`.
 * @param loop Non-owning pointer to the event loop executing socket I/O (must
 *        outlive the returned `PacketTransport`).
 * @param options Discovery and device metadata configuration.
 * @param recv_cb Callback invoked when inbound packets arrive.
 * @param socket_factory Optional custom `AsyncSocketFactory` (for testing);
 *        defaults to `LibuvAsyncSocketFactory` when `nullptr`.
 * @return Newly constructed `PacketTransport` instance.
 */
std::unique_ptr<PacketTransport> CreateWebSocketTransport(
        goldfish::async::EventLoop* loop, TransportOptions options,
        PacketTransport::RecvCallback recv_cb,
        goldfish::async::AsyncSocketFactory* socket_factory = nullptr);

/**
 * @brief Creates a WebSocket `PacketTransport` with default `TransportOptions`
 *        driven by the caller-provided `EventLoop`.
 * @param loop Non-owning pointer to the event loop executing socket I/O (must
 *        outlive the returned `PacketTransport`).
 * @param recv_cb Callback invoked when inbound packets arrive.
 * @param socket_factory Optional custom `AsyncSocketFactory` (for testing);
 *        defaults to `LibuvAsyncSocketFactory` when `nullptr`.
 * @return Newly constructed `PacketTransport` instance.
 */
std::unique_ptr<PacketTransport> CreateWebSocketTransport(
        goldfish::async::EventLoop* loop, PacketTransport::RecvCallback recv_cb,
        goldfish::async::AsyncSocketFactory* socket_factory = nullptr);

}  // namespace goldfish::netsim
