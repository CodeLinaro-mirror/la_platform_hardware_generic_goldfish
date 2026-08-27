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
#pragma once

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "goldfish/http/http_headers.h"

namespace goldfish::grpcweb {

// Parsed gRPC-Web frame view (non-owning payload slice)
struct FrameView {
    uint8_t flag = 0;
    std::string_view payload;
};

// gRPC-Web Frame Flags
inline constexpr uint8_t kFrameData = 0x00;
inline constexpr uint8_t kFrameCompressedData = 0x01;
inline constexpr uint8_t kFrameTrailer = 0x80;
inline constexpr size_t kFrameHeaderSize = 5;

// Maximum supported gRPC-Web frame payload size (64 MiB)
inline constexpr size_t kMaxFramePayloadSize = 64 * 1024 * 1024;

// Prefix for passing custom x- headers to gRPC metadata
inline constexpr std::string_view kHeaderPrefixCustom = "x-";

/**
 * @brief Utility class providing protocol-level serialization, framing, and encoding
 *        for gRPC-Web streams according to the official gRPC-Web specification.
 *
 * Threading: All methods are pure, stateless, and thread-safe.
 * Blocking: Non-blocking.
 */
class Protocol {
  public:
    /**
     * @brief Packs a payload into a 5-byte length-prefixed gRPC-Web frame.
     * @param flag 0x00 for data, 0x80 for trailing metadata.
     * @param payload Frame body.
     * @return 5-byte header (flag + 4-byte big-endian length) followed by payload bytes.
     */
    static std::string PackFrame(uint8_t flag, std::string_view payload);

    /**
     * @brief Unpacks the first 5-byte length-prefixed frame from a buffer.
     * @param data Buffer containing at least one gRPC-Web frame.
     * @param bytes_consumed Optional output pointer indicating total bytes parsed (header +
     * payload).
     * @return FrameView on success, or std::nullopt if the frame is incomplete or invalid.
     */
    static std::optional<FrameView> UnpackFrame(std::string_view data,
                                                size_t* bytes_consumed = nullptr);

    /**
     * @brief Percent-encodes a string according to RFC 3986 for grpc-message headers.
     */
    static std::string PercentEncode(std::string_view input);

    /**
     * @brief Formats gRPC status and trailing metadata into lowercase key-value lines
     *        suitable for packing into an in-band 0x80 trailer frame.
     */
    static std::string FormatTrailers(
            const grpc::Status& status,
            const std::multimap<grpc::string_ref, grpc::string_ref>& trailing_metadata);

    /**
     * @brief Parses standard gRPC timeout string (e.g. "10S", "500m", "100M", "1H")
     *        into an absolute deadline. Returns absl::InfiniteFuture() on empty or invalid input.
     */
    static absl::Time ParseGrpcTimeout(std::string_view timeout_str);

    /**
     * @brief Converts a grpc::ByteBuffer into a contiguous std::string.
     */
    static std::string ByteBufferToString(const grpc::ByteBuffer& buffer);

    /**
     * @brief Wraps a string into a grpc::ByteBuffer without unnecessary copies.
     */
    static grpc::ByteBuffer StringToByteBuffer(std::string_view str);
};

/**
 * @brief Streaming Base64 encoder for application/grpc-web-text chunked responses.
 *
 * In application/grpc-web-text, the HTTP body is a single continuous Base64 document.
 * Intermediate chunks must encode complete 3-byte triplets without adding trailing padding ('=').
 * Leftover 1-2 bytes are buffered and combined with subsequent chunks or emitted with
 * final padding upon Finalize().
 */
class StreamingBase64Encoder {
  public:
    /**
     * @brief Encodes complete 3-byte triplets from input data without padding.
     *        Retains leftover 1-2 bytes in an internal buffer.
     */
    std::string EncodeChunk(std::string_view data);

    /**
     * @brief Emits any remaining buffered bytes along with optional final data, applying
     *        standard Base64 trailing padding ('=').
     */
    std::string Finalize(std::string_view final_data = "");

    /**
     * @brief Returns the number of unencoded remainder bytes currently buffered.
     */
    size_t RemainderSize() const { return remainder_.size(); }

  private:
    std::string remainder_;
};

}  // namespace goldfish::grpcweb
