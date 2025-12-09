/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "goldfish/devices/qemud/qemud.h"

#include <cstdint>

namespace goldfish {
namespace devices {
namespace qemud {
namespace {
static unsigned parseHex1(uint8_t c) {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    } else if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    } else if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    } else {
        return 0;
    }
}
}  // namespace

void encodeRequestSize(const uint32_t size, uint8_t* const data) {
    if (size <= UINT16_MAX) {
        static const char dict[] = "0123456789ABCDEF";
        data[0] = dict[size >> 12];
        data[1] = dict[(size >> 8) & 0xF];
        data[2] = dict[(size >> 4) & 0xF];
        data[3] = dict[size & 0xF];
    } else {
        data[0] = 0x80 | (size >> 24);
        data[1] = size >> 16;
        data[2] = size >> 8;
        data[3] = size;
    }
}

size_t decodeRequestSize(const uint8_t* const data8) {
    if (data8[0] & 0x80) {
        // 31bit big-endian encoding
        return (size_t(data8[0] & 0x7F) << 24) | (size_t(data8[1]) << 16) |
               (size_t(data8[2]) << 8) | data8[3];
    } else {
        // 16bit hex encoding
        return (parseHex1(data8[0]) << 12) | (parseHex1(data8[1]) << 8) |
               (parseHex1(data8[2]) << 4) | parseHex1(data8[3]);
    }
}

Parser::Parser(Sink sink) : mSink(std::move(sink)) {}

bool Parser::onReceive(const void* const data, size_t size) {
    bool result = true;
    const uint8_t* data8 = static_cast<const uint8_t*>(data);

    while (size > 0) {
        const size_t bufSize = mBuffer.size();
        if (bufSize == 0) {
            while (size >= kSizeSize) {
                const size_t requestSize = decodeRequestSize(data8);
                if (size >= (kSizeSize + requestSize)) {
                    data8 += kSizeSize;
                    if (!mSink(data8, requestSize)) {
                        result = false;
                    }
                    data8 += requestSize;
                    size -= (kSizeSize + requestSize);
                } else {
                    break;
                }
            }

            mBuffer.assign(data8, data8 + size);  // unconsumed
            break;
        } else if (bufSize < kSizeSize) {
            const size_t consume = std::min(kSizeSize - bufSize, size);
            mBuffer.insert(mBuffer.end(), data8, data8 + consume);
            data8 += consume;
            size -= consume;
        } else if (bufSize == kSizeSize) {
            const size_t requestSize = decodeRequestSize(mBuffer.data());
            if (size >= requestSize) {
                if (!mSink(data8, requestSize)) {
                    result = false;
                }

                mBuffer.clear();
                data8 += requestSize;
                size -= requestSize;
            } else {
                const size_t consume = std::min(requestSize, size);
                mBuffer.insert(mBuffer.end(), data8, data8 + consume);
                data8 += consume;
                size -= consume;
            }
        } else {
            const size_t requestSize = decodeRequestSize(mBuffer.data());
            const size_t bufferSize = mBuffer.size();
            const size_t consume = std::min(kSizeSize + requestSize - bufferSize, size);
            mBuffer.insert(mBuffer.end(), data8, data8 + consume);
            data8 += consume;
            size -= consume;

            if ((bufferSize + consume) == (kSizeSize + requestSize)) {
                if (!mSink(&mBuffer[kSizeSize], requestSize)) {
                    result = false;
                }
                mBuffer.clear();
            }
        }
    }

    return result;
}

void Parser::saveToSnapshot(archive::IWriter& writer) const {
    writer << mBuffer.size();
    writer.write(mBuffer.data(), mBuffer.size());
}

bool Parser::loadFromSnapshot(archive::IReader& reader) {
    mBuffer.resize(getUnsigned(reader));
    return reader.read(mBuffer.data(), mBuffer.size());
}

void sendAsync(const void* data, const size_t size, cable::ISocket& dst) {
    uint8_t sizeBytes[kSizeSize];
    encodeRequestSize(size, sizeBytes);
    dst.sendAsync(sizeBytes, sizeof(sizeBytes));
    dst.sendAsync(data, size);
}

std::string encodeQemudPacket(const std::string_view data) {
    std::string result;
    result.resize(kSizeSize + data.size());
    encodeRequestSize(data.size(), (uint8_t*)result.data());
    std::memcpy(result.data() + kSizeSize, data.data(), data.size());
    return result;
}
}  // namespace qemud
}  // namespace devices
}  // namespace goldfish
