// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "goldfish/archive/deque_archive.h"
#include "goldfish/devices/qemud/qemud.h"

namespace goldfish {
using archive::DequeArchive;

namespace devices {
using cable::ISocket;
using cable::PlugPtr;

namespace qemud {

// The maximum size that can be encoded in a 4-byte hex string (FFFF)
constexpr size_t kMaxHexEncodedSize = 0xffff;

namespace {
struct TestSocket : public ISocket {
    void sendAsync(const void* const data, const size_t size) override {
        const uint8_t* const data8 = static_cast<const uint8_t*>(data);
        storage.insert(storage.end(), data8, data8 + size);
    }

    PlugPtr switchPlug(PlugPtr newPlug) override { return newPlug; }

    PlugPtr unplugImpl() override { return {}; }

    std::vector<uint8_t> storage;
};
}  // namespace

TEST(qemud, encodeRequestSize) {
    uint8_t sizeBytes[kSizeSize];
    encodeRequestSize(0x1234, sizeBytes);
    EXPECT_EQ(::strncmp("1234", (const char*)sizeBytes, kSizeSize), 0);

    encodeRequestSize(0xABCD, sizeBytes);
    EXPECT_EQ(::strncmp("ABCD", (const char*)sizeBytes, kSizeSize), 0);

    constexpr uint32_t kLongerThat16 = 123456;
    encodeRequestSize(kLongerThat16, sizeBytes);
    EXPECT_EQ(sizeBytes[0], (0x80 | (kLongerThat16 >> 24)));
    EXPECT_EQ(sizeBytes[1], (kLongerThat16 >> 16) & UCHAR_MAX);
    EXPECT_EQ(sizeBytes[2], (kLongerThat16 >> 8) & UCHAR_MAX);
    EXPECT_EQ(sizeBytes[3], kLongerThat16 & UCHAR_MAX);
}

TEST(qemud, decodeRequestSize) {
    EXPECT_EQ(decodeRequestSize((const uint8_t*)"1234"), 0x1234);
    EXPECT_EQ(decodeRequestSize((const uint8_t*)"ABCD"), 0xABCD);

    uint8_t sizeBytes[kSizeSize];
    sizeBytes[0] = 0x80 | 0x05;
    sizeBytes[1] = 0x06;
    sizeBytes[2] = 0x07;
    sizeBytes[3] = 0x08;
    EXPECT_EQ(decodeRequestSize(sizeBytes), 0x5060708);
}

TEST(qemud, loopback) {
    using namespace std::literals;

    TestSocket socket;
    constexpr auto kStr = "Hello, world!"sv;

    sendAsync(kStr.data(), kStr.size(), socket);
    EXPECT_EQ(socket.storage.size(), kSizeSize + kStr.size());
    EXPECT_EQ(decodeRequestSize(socket.storage.data()), kStr.size());
    EXPECT_EQ(::memcmp(&socket.storage[kSizeSize], kStr.data(), kStr.size()), 0);

    bool continueReceiving = true;
    unsigned payloadsReceived = 0;
    std::vector<uint8_t> payload;
    Parser parser([&payload, &payloadsReceived, &continueReceiving](const void* data, size_t size) {
        const uint8_t* data8 = static_cast<const uint8_t*>(data);
        payload.assign(data8, data8 + size);
        ++payloadsReceived;
        return continueReceiving;
    });

    for (const uint8_t b : socket.storage) {
        EXPECT_TRUE(parser.onReceive(&b, sizeof(b)));
    }

    EXPECT_EQ(payloadsReceived, 1);
    EXPECT_EQ(payload.size(), kStr.size());
    EXPECT_EQ(::memcmp(payload.data(), kStr.data(), kStr.size()), 0);

    EXPECT_TRUE(parser.onReceive(socket.storage.data(), socket.storage.size()));
    EXPECT_EQ(payloadsReceived, 2);
    EXPECT_EQ(payload.size(), kStr.size());
    EXPECT_EQ(::memcmp(payload.data(), kStr.data(), kStr.size()), 0);

    continueReceiving = false;
    EXPECT_FALSE(parser.onReceive(socket.storage.data(), socket.storage.size()));
    EXPECT_EQ(payloadsReceived, 3);
}

TEST(QemudPacketTest, EncodeEmptyPacket) {
    std::string_view data = "";
    std::string expected_packet = "0000";
    std::string actual_packet = encodeQemudPacket(data);
    EXPECT_EQ(expected_packet, actual_packet);
}

TEST(QemudPacketTest, EncodeSimplePacket) {
    std::string_view data = "test-data";
    std::string expected_header = "0009";
    std::string expected_packet = expected_header + "test-data";
    std::string actual_packet = encodeQemudPacket(data);

    EXPECT_EQ(expected_packet, actual_packet);
    EXPECT_EQ(expected_header, actual_packet.substr(0, kSizeSize));
    EXPECT_EQ(data, actual_packet.substr(kSizeSize));
}

TEST(QemudPacketTest, EncodePacketWithNulls) {
    const int kSize = 16;
    const char c_string[kSize] = "data\0with\0nulls";
    std::string data(c_string, kSize - 1);
    std::string expected_header = "000F";  // 15 bytes including nulls
    std::string expected_packet;
    expected_packet.reserve(kSizeSize + data.size());
    expected_packet.append(expected_header);
    expected_packet.append(data);

    std::string_view data_view(data);
    std::string actual_packet = encodeQemudPacket(data_view);

    EXPECT_EQ(expected_packet.size(), actual_packet.size());
    EXPECT_EQ(expected_packet, actual_packet);
}

TEST(QemudPacketTest, EncodeMaxSizedPacket) {
    std::string data(kMaxHexEncodedSize, 'a');  // 65535 'a' characters
    std::string expected_header = "FFFF";
    std::string_view data_view(data);

    std::string actual_packet = encodeQemudPacket(data_view);

    EXPECT_EQ(kSizeSize + kMaxHexEncodedSize, actual_packet.size());
    EXPECT_EQ(expected_header, actual_packet.substr(0, kSizeSize));
    EXPECT_EQ(data, actual_packet.substr(kSizeSize));
}

}  // namespace qemud
}  // namespace devices
}  // namespace goldfish
