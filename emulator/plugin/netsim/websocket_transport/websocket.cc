// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/websocket_transport/websocket.h"

#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

namespace goldfish::netsim::websocket {
namespace {

constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool IsKnown(uint8_t opcode) {
    switch (static_cast<Opcode>(opcode)) {
    case Opcode::kContinuation:
    case Opcode::kText:
    case Opcode::kBinary:
    case Opcode::kClose:
    case Opcode::kPing:
    case Opcode::kPong:
        return true;
    }
    return false;
}

}  // namespace

FrameHeader EncodeHeader(Opcode opcode, uint64_t payload_size, std::optional<MaskKey> mask,
                         bool fin) {
    FrameHeader header;
    auto push = [&header](uint8_t byte) { header.bytes[header.size++] = byte; };
    push((fin ? 0x80 : 0x00) | static_cast<uint8_t>(opcode));
    uint8_t mask_bit = mask ? 0x80 : 0x00;
    if (payload_size < 126) {
        push(mask_bit | static_cast<uint8_t>(payload_size));
    } else if (payload_size <= 0xFFFF) {
        push(mask_bit | 126);
        push(static_cast<uint8_t>(payload_size >> 8));
        push(static_cast<uint8_t>(payload_size));
    } else {
        push(mask_bit | 127);
        for (int shift = 56; shift >= 0; shift -= 8)
            push(static_cast<uint8_t>(payload_size >> shift));
    }
    if (mask) {
        for (uint8_t byte : mask->bytes) push(byte);
    }
    return header;
}

void ApplyMask(std::span<uint8_t> data, const MaskKey& mask) {
    if ((mask.bytes[0] | mask.bytes[1] | mask.bytes[2] | mask.bytes[3]) == 0U) return;

    uint8_t* ptr = data.data();
    const size_t size = data.size();
    size_t i = 0;
    while (i < size &&
           (reinterpret_cast<uintptr_t>(ptr + i) & (alignof(uint64_t) - 1U)) != 0U) {
        ptr[i] ^= mask.bytes[i & 3U];
        ++i;
    }

    const uint8_t rotated[sizeof(uint64_t)] = {
        mask.bytes[(i + 0U) & 3U], mask.bytes[(i + 1U) & 3U],
        mask.bytes[(i + 2U) & 3U], mask.bytes[(i + 3U) & 3U],
        mask.bytes[(i + 0U) & 3U], mask.bytes[(i + 1U) & 3U],
        mask.bytes[(i + 2U) & 3U], mask.bytes[(i + 3U) & 3U],
    };
    uint64_t mask64 = 0;
    std::memcpy(&mask64, rotated, sizeof(mask64));

    auto* words = reinterpret_cast<uint64_t*>(ptr + i);
    const size_t word_count = (size - i) / sizeof(uint64_t);
    size_t w = 0;
    for (; w + 4U <= word_count; w += 4U) {
        words[w + 0U] ^= mask64;
        words[w + 1U] ^= mask64;
        words[w + 2U] ^= mask64;
        words[w + 3U] ^= mask64;
    }
    for (; w < word_count; ++w) {
        words[w] ^= mask64;
    }
    i += word_count * sizeof(uint64_t);

    for (; i < size; ++i) {
        ptr[i] ^= mask.bytes[i & 3U];
    }
}

std::vector<uint8_t> EncodeFrame(Opcode opcode, std::span<const uint8_t> payload,
                                 std::optional<MaskKey> mask, bool fin) {
    FrameHeader header = EncodeHeader(opcode, payload.size(), mask, fin);
    std::vector<uint8_t> out(header.size + payload.size());
    std::memcpy(out.data(), header.bytes.data(), header.size);
    if (!payload.empty()) {
        std::memcpy(out.data() + header.size, payload.data(), payload.size());
        if (mask) ApplyMask(std::span(out).subspan(header.size), *mask);
    }
    return out;
}

absl::StatusOr<std::optional<Frame>> DecodeFrame(std::span<const uint8_t> data, size_t* consumed) {
    if (data.size() < 2) return std::nullopt;
    Frame frame{.fin = (data[0] & 0x80) != 0, .masked = (data[1] & 0x80) != 0};
    if (data[0] & 0x70) {
        return absl::DataLossError(absl::StrFormat(
                "WebSocket frame has non-zero reserved RSV bits (0x%02x in first byte 0x%02x); "
                "RFC 6455 section 5.2 requires RSV1-3 to be 0 when no extension is negotiated",
                data[0] & 0x70, data[0]));
    }
    uint8_t opcode = data[0] & 0x0F;
    if (!IsKnown(opcode)) {
        return absl::DataLossError(absl::StrFormat(
                "Unknown WebSocket opcode 0x%x (first byte 0x%02x); expected RFC 6455 opcode in "
                "{0x0..0x2, 0x8..0xA}",
                opcode, data[0]));
    }
    frame.opcode = static_cast<Opcode>(opcode);
    uint64_t len = data[1] & 0x7F;
    size_t pos = 2;
    if (len == 126 || len == 127) {
        size_t width = len == 126 ? 2 : 8;
        if (data.size() < pos + width) return std::nullopt;
        len = 0;
        for (size_t i = 0; i < width; ++i) len = (len << 8) | data[pos + i];
        pos += width;
    }
    if ((opcode & 0x8) && (len > 125 || !frame.fin)) {
        return absl::DataLossError(absl::StrFormat(
                "Invalid WebSocket control frame (opcode=0x%x, fin=%d, payload_len=%d): RFC 6455 "
                "section 5.5 requires control frames to be unfragmented (fin=1) with payload <= "
                "125 bytes",
                opcode, frame.fin, len));
    }
    if (len > kMaxPayload) {
        return absl::ResourceExhaustedError(absl::StrFormat(
                "WebSocket frame payload of %d bytes exceeds maximum allowed size of %d bytes "
                "(16 MiB)",
                len, kMaxPayload));
    }
    MaskKey mask{};
    if (frame.masked) {
        if (data.size() < pos + mask.bytes.size()) return std::nullopt;
        std::copy_n(data.begin() + pos, mask.bytes.size(), mask.bytes.begin());
        pos += mask.bytes.size();
    }
    if (data.size() - pos < len) return std::nullopt;
    frame.payload.assign(data.begin() + pos, data.begin() + pos + len);
    if (frame.masked) ApplyMask(frame.payload, mask);
    *consumed = pos + len;
    return frame;
}

std::string MakeKey() {
    std::array<uint8_t, 16> nonce{};
    if (RAND_bytes(nonce.data(), nonce.size()) != 1) {
        nonce.fill(0);
    }
    return absl::Base64Escape(
            absl::string_view(reinterpret_cast<const char*>(nonce.data()), nonce.size()));
}

std::string AcceptKey(std::string_view key) {
    std::string input = absl::StrCat(key, kGuid);
    std::array<uint8_t, SHA_DIGEST_LENGTH> digest{};
    SHA1(reinterpret_cast<const uint8_t*>(input.data()), input.size(), digest.data());
    return absl::Base64Escape(
            absl::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

MaskKey MakeMaskKey() {
    MaskKey mask{};
    if (RAND_bytes(mask.bytes.data(), mask.bytes.size()) != 1) {
        mask.bytes.fill(0);
    }
    return mask;
}

std::string HandshakeRequest(std::string_view host_port, std::string_view path_and_query,
                             std::string_view key) {
    return absl::StrCat("GET ", path_and_query, " HTTP/1.1\r\nHost: ", host_port,
                        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ", key,
                        "\r\nSec-WebSocket-Version: 13\r\n\r\n");
}

absl::Status ValidateHandshakeResponse(std::string_view head, std::string_view key) {
    std::vector<std::string_view> lines = absl::StrSplit(head, "\r\n", absl::SkipEmpty());
    if (lines.empty()) {
        return absl::UnavailableError(
                "Received empty HTTP handshake response from WebSocket server; expected "
                "'HTTP/1.1 101 Switching Protocols'");
    }
    std::vector<std::string_view> status = absl::StrSplit(lines[0], absl::MaxSplits(' ', 2));
    if (status.size() < 2 || !absl::StartsWith(status[0], "HTTP/1.") || status[1] != "101") {
        return absl::UnavailableError(
                absl::StrFormat("WebSocket handshake rejected with status line '%s'; expected "
                                "'HTTP/1.1 101 Switching Protocols'",
                                lines[0]));
    }
    bool upgrade = false, connection = false;
    std::optional<std::string_view> received_accept;
    for (size_t i = 1; i < lines.size(); ++i) {
        std::pair<std::string_view, std::string_view> header =
                absl::StrSplit(lines[i], absl::MaxSplits(':', 1));
        std::string_view name = absl::StripAsciiWhitespace(header.first);
        std::string_view value = absl::StripAsciiWhitespace(header.second);
        if (absl::EqualsIgnoreCase(name, "Upgrade")) {
            upgrade |= absl::EqualsIgnoreCase(value, "websocket");
        } else if (absl::EqualsIgnoreCase(name, "Connection")) {
            for (std::string_view token : absl::StrSplit(value, ',')) {
                connection |= absl::EqualsIgnoreCase(absl::StripAsciiWhitespace(token), "upgrade");
            }
        } else if (absl::EqualsIgnoreCase(name, "Sec-WebSocket-Accept")) {
            received_accept = value;
        }
    }
    if (!upgrade) {
        return absl::UnavailableError(
                "WebSocket handshake response missing required 'Upgrade: websocket' header");
    }
    if (!connection) {
        return absl::UnavailableError(
                "WebSocket handshake response missing required 'Connection: Upgrade' header");
    }
    std::string expected_accept = AcceptKey(key);
    if (!received_accept) {
        return absl::UnavailableError(absl::StrFormat(
                "WebSocket handshake response missing required 'Sec-WebSocket-Accept' header "
                "(expected '%s')",
                expected_accept));
    }
    if (*received_accept != expected_accept) {
        return absl::UnavailableError(absl::StrFormat(
                "WebSocket handshake 'Sec-WebSocket-Accept' mismatch: got '%s', expected '%s'",
                *received_accept, expected_accept));
    }
    return absl::OkStatus();
}

std::span<const uint8_t> StreamParser::PeekContiguous() {
    auto [ptr, len] = read_buffer_.Peek();
    if (len < read_buffer_.Size()) {
        std::vector<uint8_t> linear(read_buffer_.Size());
        std::memcpy(linear.data(), ptr, len);
        (void)read_buffer_.Consume(len);
        auto [ptr2, len2] = read_buffer_.Peek();
        std::memcpy(linear.data() + len, ptr2, len2);
        (void)read_buffer_.Consume(len2);
        (void)read_buffer_.Append(linear.data(), linear.size());
        std::tie(ptr, len) = read_buffer_.Peek();
    }
    return {static_cast<const uint8_t*>(ptr), len};
}

void StreamParser::Append(std::span<const uint8_t> data) {
    if (data.empty()) return;
    (void)read_buffer_.Append(data.data(), data.size());
}

absl::StatusOr<bool> StreamParser::ConsumeHandshake(std::string_view expected_key) {
    std::span<const uint8_t> view = PeekContiguous();
    std::string_view head(reinterpret_cast<const char*>(view.data()), view.size());
    size_t end = head.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        if (head.size() > kMaxHandshakeBytes) {
            return absl::ResourceExhaustedError(absl::StrFormat(
                    "WebSocket handshake response headers (%d bytes) exceed maximum allowed "
                    "size of %d bytes (64 KiB)",
                    head.size(), kMaxHandshakeBytes));
        }
        return false;
    }
    if (end + 4 > kMaxHandshakeBytes) {
        return absl::ResourceExhaustedError(absl::StrFormat(
                "WebSocket handshake response headers (%d bytes) exceed maximum allowed "
                "size of %d bytes (64 KiB)",
                end + 4, kMaxHandshakeBytes));
    }
    std::string header_block(head.substr(0, end + 4));
    (void)read_buffer_.Consume(header_block.size());
    absl::Status status = ValidateHandshakeResponse(header_block, expected_key);
    if (!status.ok()) return status;
    return true;
}

absl::StatusOr<std::optional<StreamEvent>> StreamParser::NextEvent() {
    while (true) {
        auto [ptr, len] = read_buffer_.Peek();
        std::span<const uint8_t> view(static_cast<const uint8_t*>(ptr), len);
        size_t consumed = 0;
        absl::StatusOr<std::optional<Frame>> decoded = DecodeFrame(view, &consumed);
        if (decoded.ok() && !decoded->has_value() && len < read_buffer_.Size()) {
            view = PeekContiguous();
            decoded = DecodeFrame(view, &consumed);
        }
        if (!decoded.ok()) return decoded.status();
        if (!decoded->has_value()) return std::nullopt;
        (void)read_buffer_.Consume(consumed);
        Frame frame = **std::move(decoded);
        if (frame.masked) {
            return absl::DataLossError(absl::StrFormat(
                    "WebSocket protocol error: server sent masked frame (opcode=0x%x, "
                    "payload_len=%d); RFC 6455 section 5.1 requires server-to-client frames "
                    "to be unmasked",
                    static_cast<uint8_t>(frame.opcode), frame.payload.size()));
        }
        switch (frame.opcode) {
        case Opcode::kPing:
            return StreamEvent{
                .kind = StreamEvent::Kind::kPing,
                .payload = std::move(frame.payload),
            };
        case Opcode::kPong:
            continue;
        case Opcode::kClose:
            return StreamEvent{
                .kind = StreamEvent::Kind::kClose,
                .payload = std::move(frame.payload),
            };
        case Opcode::kText:
        case Opcode::kBinary:
            if (in_binary_ || in_text_) {
                return absl::DataLossError(absl::StrFormat(
                        "WebSocket protocol error: received new data frame (opcode=0x%x, "
                        "fin=%d) while a fragmented %s message is in progress; RFC 6455 "
                        "section 5.4 requires continuation frames (opcode=0x0) until fin=1",
                        static_cast<uint8_t>(frame.opcode), frame.fin,
                        in_binary_ ? "binary" : "text"));
            }
            if (frame.opcode == Opcode::kText) {
                in_text_ = !frame.fin;
                continue;
            }
            message_ = std::move(frame.payload);
            break;
        case Opcode::kContinuation:
            if (in_text_) {
                in_text_ = !frame.fin;
                continue;
            }
            if (!in_binary_) {
                return absl::DataLossError(absl::StrFormat(
                        "WebSocket protocol error: received continuation frame (opcode=0x0, "
                        "fin=%d, payload_len=%d) with no fragmented message in progress",
                        frame.fin, frame.payload.size()));
            }
            if (message_.size() + frame.payload.size() > kMaxPayload) {
                return absl::ResourceExhaustedError(absl::StrFormat(
                        "WebSocket fragmented message size (%d bytes) exceeds maximum allowed "
                        "size of %d bytes (16 MiB)",
                        message_.size() + frame.payload.size(), kMaxPayload));
            }
            message_.insert(message_.end(), frame.payload.begin(), frame.payload.end());
            break;
        }
        in_binary_ = !frame.fin;
        if (in_binary_) continue;

        return StreamEvent{
            .kind = StreamEvent::Kind::kBinaryMessage,
            .payload = std::exchange(message_, {}),
        };
    }
}

void StreamParser::Reset() {
    read_buffer_.Clear(/*also_free_memory=*/false);
    message_.clear();
    in_binary_ = false;
    in_text_ = false;
}

}  // namespace goldfish::netsim::websocket
