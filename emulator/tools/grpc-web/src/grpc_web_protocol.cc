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

#include <charconv>
#include <cstring>
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

namespace {

inline bool IsReservedTrailerHeader(std::string_view key) {
    return absl::EqualsIgnoreCase(key, http::headers::kGrpcStatus) ||
           absl::EqualsIgnoreCase(key, http::headers::kGrpcMessage) ||
           absl::EqualsIgnoreCase(key, http::headers::kGrpcStatusDetailsBin);
}

std::optional<absl::Duration> ParseTimeoutUnit(char unit, uint64_t val) {
    switch (unit) {
    case 'H':
        return absl::Hours(val);
    case 'M':
        return absl::Minutes(val);
    case 'S':
        return absl::Seconds(val);
    case 'm':
        return absl::Milliseconds(val);
    case 'u':
        return absl::Microseconds(val);
    case 'n':
        return absl::Nanoseconds(val);
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
    for (unsigned char c : input) {
        if (absl::ascii_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped.push_back(static_cast<char>(c));
        } else {
            escaped.push_back('%');
            escaped.push_back(kHexDigits[(c >> 4) & 0x0F]);
            escaped.push_back(kHexDigits[c & 0x0F]);
        }
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
        std::string_view key(key_ref.data(), key_ref.size());
        if (!IsReservedTrailerHeader(key)) {
            absl::StrAppend(&out, absl::AsciiStrToLower(key), http::protocol::kColon,
                            std::string_view(val_ref.data(), val_ref.size()),
                            http::protocol::kCrlf);
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
    std::string input;
    if (!remainder_.empty()) {
        input.reserve(remainder_.size() + data.size());
        input.append(remainder_);
        input.append(data);
        remainder_.clear();
    } else {
        input = std::string(data);
    }

    size_t complete_triplets_len = (input.size() / 3) * 3;
    std::string output;
    if (complete_triplets_len > 0) {
        output = absl::Base64Escape(std::string_view(input.data(), complete_triplets_len));
    }
    if (input.size() > complete_triplets_len) {
        remainder_ = input.substr(complete_triplets_len);
    }
    return output;
}

std::string StreamingBase64Encoder::Finalize(std::string_view final_data) {
    std::string input;
    if (!remainder_.empty() || !final_data.empty()) {
        input.reserve(remainder_.size() + final_data.size());
        input.append(remainder_);
        input.append(final_data);
        remainder_.clear();
    }
    if (input.empty()) {
        return "";
    }
    return absl::Base64Escape(input);
}

}  // namespace goldfish::grpcweb
