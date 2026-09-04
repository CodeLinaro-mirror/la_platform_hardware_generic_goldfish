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

#include <gtest/gtest.h>

#include "absl/strings/escaping.h"

namespace goldfish::grpcweb {

TEST(GrpcWebProtocolTest, PackAndUnpackFrame) {
    std::string payload = "Hello gRPC-Web World!";
    std::string packed = Protocol::PackFrame(kFrameData, payload);

    EXPECT_EQ(packed.size(), kFrameHeaderSize + payload.size());
    EXPECT_EQ(static_cast<uint8_t>(packed[0]), kFrameData);

    auto frame_opt = Protocol::UnpackFrame(packed);
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->flag, kFrameData);
    EXPECT_EQ(frame_opt->payload, payload);
}

TEST(GrpcWebProtocolTest, PackAndUnpackEmptyPayload) {
    std::string packed = Protocol::PackFrame(kFrameTrailer, "");
    EXPECT_EQ(packed.size(), kFrameHeaderSize);

    auto frame_opt = Protocol::UnpackFrame(packed);
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->flag, kFrameTrailer);
    EXPECT_EQ(frame_opt->payload, "");
}

TEST(GrpcWebProtocolTest, UnpackIncompleteFrameFails) {
    std::string packed = Protocol::PackFrame(kFrameData, "abcdef");

    // Less than header size
    EXPECT_FALSE(Protocol::UnpackFrame(packed.substr(0, 3)).has_value());

    // Truncated payload
    EXPECT_FALSE(Protocol::UnpackFrame(packed.substr(0, kFrameHeaderSize + 2)).has_value());
}

TEST(GrpcWebProtocolTest, UnpackRejectsInvalidFlag) {
    std::string invalid_flag_frame = std::string("\x42\x00\x00\x00\x03", 5) + "abc";
    EXPECT_FALSE(Protocol::UnpackFrame(invalid_flag_frame).has_value());
}

TEST(GrpcWebProtocolTest, UnpackRejectsExcessiveLength) {
    // 5-byte header with length exceeding kMaxFramePayloadSize (e.g. 128 MiB = 0x08000000)
    std::string frame = std::string("\x00\x08\x00\x00\x00", 5) + "abc";
    EXPECT_FALSE(Protocol::UnpackFrame(frame).has_value());
}

TEST(GrpcWebProtocolTest, PercentEncoding) {
    EXPECT_EQ(Protocol::PercentEncode("simple"), "simple");
    EXPECT_EQ(Protocol::PercentEncode("Hello World!"), "Hello%20World%21");
    EXPECT_EQ(Protocol::PercentEncode("a/b?c=d&e"), "a%2Fb%3Fc%3Dd%26e");
}

TEST(GrpcWebProtocolTest, FormatTrailers) {
    grpc::Status status(grpc::StatusCode::NOT_FOUND, "Entity not found");
    std::multimap<grpc::string_ref, grpc::string_ref> metadata;
    metadata.emplace("X-Custom-Header", "Value123");

    std::string trailers = Protocol::FormatTrailers(status, metadata);

    EXPECT_NE(trailers.find("grpc-status:5\r\n"), std::string::npos);
    EXPECT_NE(trailers.find("grpc-message:Entity%20not%20found\r\n"), std::string::npos);
    EXPECT_NE(trailers.find("x-custom-header:Value123\r\n"), std::string::npos);
}

TEST(GrpcWebProtocolTest, ParseGrpcTimeout) {
    auto now = absl::Now();
    auto deadline = Protocol::ParseGrpcTimeout("10S");
    auto duration = deadline - now;
    EXPECT_GE(duration, absl::Seconds(9));
    EXPECT_LE(duration, absl::Seconds(11));

    EXPECT_EQ(Protocol::ParseGrpcTimeout(""), absl::InfiniteFuture());
    EXPECT_EQ(Protocol::ParseGrpcTimeout("invalid"), absl::InfiniteFuture());
    EXPECT_EQ(Protocol::ParseGrpcTimeout("10X"), absl::InfiniteFuture());

    auto deadline_ms = Protocol::ParseGrpcTimeout("500m");
    EXPECT_GE(deadline_ms - now, absl::Milliseconds(400));
    EXPECT_LE(deadline_ms - now, absl::Milliseconds(600));

    // Test large timeout values
    EXPECT_GT(Protocol::ParseGrpcTimeout("999999999H"), now + absl::Hours(10000));
}

TEST(GrpcWebProtocolTest, ByteBufferConversion) {
    std::string original = "Buffer data with \0 zero byte";
    grpc::ByteBuffer bb = Protocol::StringToByteBuffer(original);
    std::string converted = Protocol::ByteBufferToString(bb);
    EXPECT_EQ(original, converted);
}

TEST(GrpcWebProtocolTest, UnpackFrameValueObject) {
    std::string payload = "Value object slice test";
    std::string packed = Protocol::PackFrame(kFrameData, payload);

    size_t consumed = 0;
    auto frame_opt = Protocol::UnpackFrame(packed, &consumed);
    ASSERT_TRUE(frame_opt.has_value());
    EXPECT_EQ(frame_opt->flag, kFrameData);
    EXPECT_EQ(frame_opt->payload, payload);
    EXPECT_EQ(consumed, kFrameHeaderSize + payload.size());

    // Incomplete data returns nullopt
    EXPECT_FALSE(Protocol::UnpackFrame(packed.substr(0, 3)).has_value());
    EXPECT_FALSE(Protocol::UnpackFrame(packed.substr(0, packed.size() - 1)).has_value());
}

TEST(GrpcWebProtocolTest, StreamingBase64EncoderPartialChunks) {
    StreamingBase64Encoder encoder;

    // Chunk 1: 4 bytes ("ABCD") -> 3 bytes encoded ("QUJD"), 1 byte buffered ("D")
    std::string out1 = encoder.EncodeChunk("ABCD");
    EXPECT_EQ(out1, "QUJD");
    EXPECT_EQ(encoder.RemainderSize(), 1);
    EXPECT_FALSE(absl::StrContains(out1, "="));

    // Chunk 2: 4 bytes ("EFGH") -> buffered "D" + "EFGH" = "DEFGH" -> 3 bytes encoded ("REVG"), 2
    // bytes buffered ("H")
    std::string out2 = encoder.EncodeChunk("EFGH");
    EXPECT_EQ(out2, "REVG");
    EXPECT_EQ(encoder.RemainderSize(), 2);
    EXPECT_FALSE(absl::StrContains(out2, "="));

    // Finalize with 3 bytes ("IJK") -> buffered "GH" + "IJK" = "GHIJK" (5 bytes) -> 3 bytes + 2
    // bytes padded ("R0hJSks=")
    std::string out3 = encoder.Finalize("IJK");
    EXPECT_EQ(out3, "R0hJSks=");
    EXPECT_EQ(encoder.RemainderSize(), 0);

    // Verify concatenated output decodes to the complete original data "ABCDEFGHIJK"
    std::string concatenated = absl::StrCat(out1, out2, out3);
    std::string decoded;
    ASSERT_TRUE(absl::Base64Unescape(concatenated, &decoded));
    EXPECT_EQ(decoded, "ABCDEFGHIJK");
    EXPECT_EQ(concatenated, absl::Base64Escape("ABCDEFGHIJK"));
}

TEST(GrpcWebProtocolTest, StreamingBase64EncoderSingleByteFeeds) {
    std::string original = "Testing arbitrary streaming Base64 encoding across odd byte boundaries";
    StreamingBase64Encoder encoder;
    std::string stream;

    // Feed 1 byte at a time
    for (size_t i = 0; i < original.size() - 1; ++i) {
        std::string chunk = encoder.EncodeChunk(original.substr(i, 1));
        // Intermediate chunks must NEVER contain '=' padding
        EXPECT_FALSE(absl::StrContains(chunk, "="));
        stream.append(chunk);
    }

    // Finalize with the last byte
    std::string final_chunk = encoder.Finalize(original.substr(original.size() - 1, 1));
    stream.append(final_chunk);

    EXPECT_EQ(stream, absl::Base64Escape(original));
    std::string decoded;
    ASSERT_TRUE(absl::Base64Unescape(stream, &decoded));
    EXPECT_EQ(decoded, original);
}

}  // namespace goldfish::grpcweb
