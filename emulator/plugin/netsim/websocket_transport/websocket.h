// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file websocket.h
 * @brief RFC 6455 WebSocket client handshake, frame codec, and stateful stream
 *        parser with fragment reassembly.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "goldfish/socket_buffer.h"

namespace goldfish::netsim::websocket {

/**
 * @brief RFC 6455 section 5.2 WebSocket frame opcodes.
 */
enum class Opcode : uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

/**
 * @brief Strong type representing a 4-byte masking key for client-to-server
 *        frames (RFC 6455 section 5.3).
 */
struct MaskKey {
    std::array<uint8_t, 4> bytes{};
    friend bool operator==(const MaskKey&, const MaskKey&) = default;
};

/**
 * @brief Decoded RFC 6455 WebSocket frame.
 */
struct Frame {
    bool fin = true;
    Opcode opcode = Opcode::kBinary;
    bool masked = false;
    std::vector<uint8_t> payload;
};

/// Maximum permitted payload size for a frame or reassembled message (16 MiB).
inline constexpr uint64_t kMaxPayload = 16 * 1024 * 1024;

/**
 * @brief Serialized RFC 6455 frame header buffer.
 */
struct FrameHeader {
    // Maximum RFC 6455 frame header size: 2 bytes base header + 8 bytes
    // extended payload length + 4 bytes masking key.
    std::array<uint8_t, 14> bytes{};
    uint8_t size = 0;
    std::span<const uint8_t> span() const { return {bytes.data(), size}; }
};

/**
 * @brief Encodes an RFC 6455 frame header.
 * @param opcode Frame opcode.
 * @param payload_size Payload byte length.
 * @param mask Optional 4-byte client masking key.
 * @param fin True if this is the final fragment of a message.
 * @return Serialized FrameHeader.
 */
FrameHeader EncodeHeader(Opcode opcode, uint64_t payload_size, std::optional<MaskKey> mask,
                         bool fin = true);

/**
 * @brief Applies an in-place 4-byte XOR mask to `data` (RFC 6455 section 5.3).
 * @param data Payload buffer to mask or unmask in place.
 * @param mask 4-byte masking key.
 */
void ApplyMask(std::span<uint8_t> data, const MaskKey& mask);

/**
 * @brief Encodes a complete WebSocket frame (header + masked/unmasked payload).
 * @param opcode Frame opcode.
 * @param payload Frame payload bytes.
 * @param mask Optional 4-byte masking key.
 * @param fin True if this is the final fragment.
 * @return Serialized frame bytes.
 */
std::vector<uint8_t> EncodeFrame(Opcode opcode, std::span<const uint8_t> payload,
                                 std::optional<MaskKey> mask, bool fin = true);

/**
 * @brief Decodes a single WebSocket frame from `data`.
 * @param data Input byte span.
 * @param consumed Output receiving the number of bytes consumed when a frame is decoded.
 * @return Decoded Frame, `std::nullopt` if `data` holds an incomplete frame, or
 *         an error status if the frame violates RFC 6455.
 */
absl::StatusOr<std::optional<Frame>> DecodeFrame(std::span<const uint8_t> data, size_t* consumed);

/**
 * @brief Generates a random 16-byte base64-encoded `Sec-WebSocket-Key` nonce.
 */
std::string MakeKey();

/**
 * @brief Computes the expected `Sec-WebSocket-Accept` value for `key` (RFC 6455 section 4.2.2).
 */
std::string AcceptKey(std::string_view key);

/**
 * @brief Generates a random 4-byte client frame masking key.
 */
MaskKey MakeMaskKey();

/**
 * @brief Formats an HTTP/1.1 WebSocket upgrade handshake request.
 * @param host_port Value for the HTTP `Host` header.
 * @param path_and_query Request-target path and query string.
 * @param key Base64-encoded `Sec-WebSocket-Key` nonce.
 * @return HTTP/1.1 request string terminated with `\r\n\r\n`.
 */
std::string HandshakeRequest(std::string_view host_port, std::string_view path_and_query,
                             std::string_view key);

/**
 * @brief Validates the server's HTTP/1.1 101 Switching Protocols response headers.
 * @param head Raw HTTP response header block.
 * @param key Client `Sec-WebSocket-Key` nonce used to verify `Sec-WebSocket-Accept`.
 * @return `absl::OkStatus()` if valid, or `UnavailableError` detailing the mismatch.
 */
absl::Status ValidateHandshakeResponse(std::string_view head, std::string_view key);

/**
 * @brief High-level protocol event produced by `StreamParser`.
 */
struct StreamEvent {
    enum class Kind {
        /// Complete binary message (single-frame or reassembled from fragments).
        kBinaryMessage,
        /// Ping control frame; caller should respond with a Pong frame carrying `payload`.
        kPing,
        /// Close control frame; caller should echo Close and close the connection.
        kClose,
    };
    Kind kind = Kind::kBinaryMessage;
    std::vector<uint8_t> payload;
};

/**
 * @brief Stateful RFC 6455 client stream parser encapsulating receive buffering,
 *        HTTP upgrade header consumption, and fragmented frame reassembly.
 */
class StreamParser {
  public:
    /// Maximum allowed HTTP handshake response header size (64 KiB).
    static constexpr size_t kMaxHandshakeBytes = 64 * 1024;

    /**
     * @brief Appends raw bytes received from the socket into the parser buffer.
     * @param data Raw bytes read from the transport socket.
     */
    void Append(std::span<const uint8_t> data);

    /**
     * @brief Consumes and validates the HTTP/1.1 101 upgrade response once `\r\n\r\n` arrives.
     * @param expected_key Client `Sec-WebSocket-Key` nonce sent in the upgrade request.
     * @return `true` if the handshake completed, `false` if more bytes are needed,
     *         or an error status if the response is invalid or exceeds 64 KiB.
     */
    absl::StatusOr<bool> ConsumeHandshake(std::string_view expected_key);

    /**
     * @brief Decodes the next complete binary message or control event from buffered frames.
     * @return Next `StreamEvent`, `std::nullopt` if more bytes are needed, or an
     *         error status on RFC 6455 framing/fragmentation violations.
     */
    absl::StatusOr<std::optional<StreamEvent>> NextEvent();

    /**
     * @brief Clears all buffered data and in-progress fragment state.
     */
    void Reset();

    /**
     * @brief Returns the number of unconsumed bytes currently buffered.
     */
    size_t UnconsumedBytes() const { return read_buffer_.Size(); }

  private:
    std::span<const uint8_t> PeekContiguous();

    goldfish::SocketBuffer read_buffer_;
    std::vector<uint8_t> message_;
    bool in_binary_ = false;
    bool in_text_ = false;
};

}  // namespace goldfish::netsim::websocket
