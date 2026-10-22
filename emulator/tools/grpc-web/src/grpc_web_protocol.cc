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
#include "goldfish/grpcweb/grpc_web_protocol.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace goldfish::grpcweb {

std::string Protocol::PackFrame(uint8_t flag, std::string_view payload) {
    if (payload.size() > kMaxFramePayloadSize) {
        LOG(ERROR) << "Payload size " << payload.size() << " exceeds maximum frame limit "
                   << kMaxFramePayloadSize;
        return "";
    }

    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    frame.push_back(static_cast<char>(flag));

    uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
    const char* len_bytes = reinterpret_cast<const char*>(&net_len);
    frame.append(len_bytes, sizeof(net_len));
    frame.append(payload);
    return frame;
}

std::string Protocol::PackFrame(uint8_t flag, const grpc::ByteBuffer& payload) {
    const size_t payload_size = payload.Length();
    if (payload_size > kMaxFramePayloadSize) {
        LOG(ERROR) << "Failed to pack gRPC-Web frame: payload size (" << payload_size
                   << " bytes) exceeds maximum allowed limit (" << kMaxFramePayloadSize
                   << " bytes)";
        return "";
    }

    std::string frame;
    frame.reserve(kFrameHeaderSize + payload_size);
    frame.push_back(static_cast<char>(flag));

    const uint32_t net_len = htonl(static_cast<uint32_t>(payload_size));
    const char* len_bytes = reinterpret_cast<const char*>(&net_len);
    frame.append(len_bytes, sizeof(net_len));
    if (payload_size == 0) {
        return frame;
    }

    grpc::Slice single_slice;
    if (payload.TrySingleSlice(&single_slice).ok()) {
        frame.append(reinterpret_cast<const char*>(single_slice.begin()), single_slice.size());
        return frame;
    }

    std::vector<grpc::Slice> slices;
    grpc::Status status = payload.Dump(&slices);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to dump gRPC ByteBuffer slices (size=" << payload_size
                   << " bytes): " << status.error_message() << " (code=" << status.error_code()
                   << ")";
        return "";
    }
    for (const auto& slice : slices) {
        frame.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
    }
    return frame;
}

namespace {

inline bool IsReservedTrailerHeader(std::string_view key) {
    return absl::EqualsIgnoreCase(key, http::headers::kGrpcStatus) ||
           absl::EqualsIgnoreCase(key, http::headers::kGrpcMessage) ||
           absl::EqualsIgnoreCase(key, http::headers::kGrpcStatusDetailsBin);
}

inline bool IsUnreservedUriChar(unsigned char c) {
    return absl::ascii_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

inline std::string StripCrlf(std::string_view input) {
    size_t pos = input.find_first_of("\r\n");
    if (pos == std::string_view::npos) {
        return std::string(input);
    }
    std::string out;
    out.reserve(input.size());
    size_t start = 0;
    do {
        out.append(input.data() + start, pos - start);
        start = pos + 1;
        pos = input.find_first_of("\r\n", start);
    } while (pos != std::string_view::npos);
    out.append(input.data() + start, input.size() - start);
    return out;
}

// Maximum timeout clamped to 100 years to prevent int64_t nanosecond overflow
// when converting to std::chrono::system_clock::time_point.
inline constexpr absl::Duration kMaxGrpcTimeout = absl::Hours(24 * 365 * 100);

std::optional<absl::Duration> ParseTimeoutUnit(char unit, uint64_t val) {
    constexpr uint64_t kMaxSafeVal = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    int64_t safe_val = static_cast<int64_t>(std::min(val, kMaxSafeVal));
    switch (unit) {
    case 'H':
        return std::min(absl::Hours(safe_val), kMaxGrpcTimeout);
    case 'M':
        return std::min(absl::Minutes(safe_val), kMaxGrpcTimeout);
    case 'S':
        return std::min(absl::Seconds(safe_val), kMaxGrpcTimeout);
    case 'm':
        return std::min(absl::Milliseconds(safe_val), kMaxGrpcTimeout);
    case 'u':
        return std::min(absl::Microseconds(safe_val), kMaxGrpcTimeout);
    case 'n':
        return std::min(absl::Nanoseconds(safe_val), kMaxGrpcTimeout);
    default:
        return std::nullopt;
    }
}

}  // namespace

std::optional<FrameView> Protocol::UnpackFrame(std::string_view data, size_t* bytes_consumed) {
    if (data.size() < kFrameHeaderSize) {
        return std::nullopt;
    }

    uint8_t flag = static_cast<uint8_t>(data[0]);
    if (flag != kFrameData && flag != kFrameCompressedData && flag != kFrameTrailer) {
        return std::nullopt;
    }

    uint32_t net_len = 0;
    std::memcpy(&net_len, data.data() + 1, sizeof(net_len));
    uint32_t length = ntohl(net_len);

    if (length > kMaxFramePayloadSize || data.size() - kFrameHeaderSize < length) {
        return std::nullopt;
    }

    if (bytes_consumed != nullptr) {
        *bytes_consumed = kFrameHeaderSize + length;
    }

    return FrameView{flag, data.substr(kFrameHeaderSize, length)};
}

std::string Protocol::PercentEncode(std::string_view input) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(input.size());
    size_t span_start = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (!IsUnreservedUriChar(c)) {
            if (i > span_start) {
                escaped.append(input.data() + span_start, i - span_start);
            }
            const char pct[3] = {'%', kHexDigits[(c >> 4) & 0x0F], kHexDigits[c & 0x0F]};
            escaped.append(pct, sizeof(pct));
            span_start = i + 1;
        }
    }
    if (span_start < input.size()) {
        escaped.append(input.data() + span_start, input.size() - span_start);
    }
    return escaped;
}

std::string Protocol::FormatTrailers(
        const grpc::Status& status,
        const std::multimap<grpc::string_ref, grpc::string_ref>& trailing_metadata) {
    std::string out;
    absl::StrAppend(&out, http::headers::kGrpcStatus, http::protocol::kColon, status.error_code(),
                    http::protocol::kCrlf);

    if (!status.error_message().empty()) {
        absl::StrAppend(&out, http::headers::kGrpcMessage, http::protocol::kColon,
                        PercentEncode(status.error_message()), http::protocol::kCrlf);
    }
    if (!status.error_details().empty()) {
        absl::StrAppend(&out, http::headers::kGrpcStatusDetailsBin, http::protocol::kColon,
                        absl::Base64Escape(status.error_details()), http::protocol::kCrlf);
    }

    for (const auto& [key_ref, val_ref] : trailing_metadata) {
        std::string_view raw_key(key_ref.data(), key_ref.size());
        if (!IsReservedTrailerHeader(raw_key)) {
            std::string key = StripCrlf(raw_key);
            absl::AsciiStrToLower(&key);
            std::string_view raw_val(val_ref.data(), val_ref.size());
            std::string val =
                    absl::EndsWith(key, "-bin") ? absl::Base64Escape(raw_val) : StripCrlf(raw_val);
            absl::StrAppend(&out, key, http::protocol::kColon, val, http::protocol::kCrlf);
        }
    }

    return out;
}

absl::Time Protocol::ParseGrpcTimeout(std::string_view timeout_str) {
    if (timeout_str.empty()) {
        return absl::InfiniteFuture();
    }

    char unit = timeout_str.back();
    std::string_view val_str = timeout_str.substr(0, timeout_str.size() - 1);
    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(val_str.data(), val_str.data() + val_str.size(), val);
    if (ec != std::errc() || ptr != val_str.data() + val_str.size()) {
        return absl::InfiniteFuture();
    }

    auto duration = ParseTimeoutUnit(unit, val);
    if (!duration.has_value()) {
        return absl::InfiniteFuture();
    }

    return absl::Now() + *duration;
}

std::string Protocol::ByteBufferToString(const grpc::ByteBuffer& buffer) {
    if (buffer.Length() == 0) {
        return "";
    }
    grpc::Slice single_slice;
    if (buffer.TrySingleSlice(&single_slice).ok()) {
        return std::string(reinterpret_cast<const char*>(single_slice.begin()),
                           single_slice.size());
    }
    std::vector<grpc::Slice> slices;
    grpc::Status status = buffer.Dump(&slices);
    if (!status.ok()) {
        return "";
    }
    std::string result;
    result.reserve(buffer.Length());
    for (const auto& slice : slices) {
        result.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
    }
    return result;
}

grpc::ByteBuffer Protocol::StringToByteBuffer(std::string_view str) {
    grpc::Slice slice(str.empty() ? "" : str.data(), str.size());
    return grpc::ByteBuffer(&slice, 1);
}

std::string StreamingBase64Encoder::EncodeChunk(std::string_view data) {
    if (data.empty()) {
        return "";
    }
    if (remainder_.empty()) {
        const size_t complete_triplets_len = (data.size() / 3) * 3;
        remainder_.assign(data.substr(complete_triplets_len));
        if (complete_triplets_len == 0) {
            return "";
        }
        return absl::Base64Escape(data.substr(0, complete_triplets_len));
    }

    const size_t total_len = remainder_.size() + data.size();
    if (total_len < 3) {
        remainder_.append(data);
        return "";
    }

    const size_t complete_triplets_len = (total_len / 3) * 3;
    const size_t data_triplets_len = complete_triplets_len - remainder_.size();
    if (complete_triplets_len == 3) {
        remainder_.append(data.data(), data_triplets_len);
        std::string output = absl::Base64Escape(remainder_);
        remainder_.assign(data.substr(data_triplets_len));
        return output;
    }

    std::string input;
    input.reserve(complete_triplets_len);
    input.append(remainder_);
    input.append(data.data(), data_triplets_len);
    remainder_.assign(data.substr(data_triplets_len));
    return absl::Base64Escape(input);
}

std::string StreamingBase64Encoder::Finalize(std::string_view final_data) {
    if (remainder_.empty()) {
        return final_data.empty() ? "" : absl::Base64Escape(final_data);
    }
    if (final_data.empty()) {
        std::string output = absl::Base64Escape(remainder_);
        remainder_.clear();
        return output;
    }
    std::string input;
    input.reserve(remainder_.size() + final_data.size());
    input.append(remainder_);
    input.append(final_data);
    remainder_.clear();
    return absl::Base64Escape(input);
}

}  // namespace goldfish::grpcweb
