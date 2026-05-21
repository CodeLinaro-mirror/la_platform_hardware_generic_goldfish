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

namespace goldfish::devices::qemud {
namespace {
unsigned ParseHex1(uint8_t c) {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    return 0;
}
}  // namespace

void EncodeRequestSize(const uint32_t size, uint8_t* const data) {
    if (size <= UINT16_MAX) {
        static const char kDict[] = "0123456789ABCDEF";
        data[0] = kDict[size >> 12];
        data[1] = kDict[(size >> 8) & 0xF];
        data[2] = kDict[(size >> 4) & 0xF];
        data[3] = kDict[size & 0xF];
    } else {
        data[0] = 0x80 | (size >> 24);
        data[1] = size >> 16;
        data[2] = size >> 8;
        data[3] = size;
    }
}

size_t DecodeRequestSize(const uint8_t* const data8) {
    if (data8[0] & 0x80) {
        // 31bit big-endian encoding
        return (static_cast<size_t>(data8[0] & 0x7F) << 24) |
               (static_cast<size_t>(data8[1]) << 16) | (static_cast<size_t>(data8[2]) << 8) |
               data8[3];
    }  // 16bit hex encoding
    return (ParseHex1(data8[0]) << 12) | (ParseHex1(data8[1]) << 8) | (ParseHex1(data8[2]) << 4) |
           ParseHex1(data8[3]);
}

Parser::Parser(Sink sink) : sink_(std::move(sink)) {}

// TODO: This triggers clang-tidy complexity
bool Parser::OnReceive(const void* const data, size_t size) {  // NOLINT
    bool result = true;
    const auto* data8 = static_cast<const uint8_t*>(data);

    while (size > 0) {
        const size_t buf_size = buffer_.size();
        if (buf_size == 0) {
            while (size >= kSizeSize) {
                const size_t request_size = DecodeRequestSize(data8);
                if (size >= (kSizeSize + request_size)) {
                    data8 += kSizeSize;
                    if (!sink_(data8, request_size)) {
                        result = false;
                    }
                    data8 += request_size;
                    size -= (kSizeSize + request_size);
                } else {
                    break;
                }
            }

            buffer_.assign(data8, data8 + size);  // unconsumed
            break;
        }
        if (buf_size < kSizeSize) {
            const size_t consume = std::min(kSizeSize - buf_size, size);
            buffer_.insert(buffer_.end(), data8, data8 + consume);
            data8 += consume;
            size -= consume;
        } else if (buf_size == kSizeSize) {
            const size_t request_size = DecodeRequestSize(buffer_.data());
            if (size >= request_size) {
                if (!sink_(data8, request_size)) {
                    result = false;
                }

                buffer_.clear();
                data8 += request_size;
                size -= request_size;
            } else {
                const size_t consume = std::min(request_size, size);
                buffer_.insert(buffer_.end(), data8, data8 + consume);
                data8 += consume;
                size -= consume;
            }
        } else {
            const size_t request_size = DecodeRequestSize(buffer_.data());
            const size_t buffer_size = buffer_.size();
            const size_t consume = std::min(kSizeSize + request_size - buffer_size, size);
            buffer_.insert(buffer_.end(), data8, data8 + consume);
            data8 += consume;
            size -= consume;

            if ((buffer_size + consume) == (kSizeSize + request_size)) {
                if (!sink_(&buffer_[kSizeSize], request_size)) {
                    result = false;
                }
                buffer_.clear();
            }
        }
    }

    return result;
}

void Parser::SaveToSnapshot(archive::IWriter& writer) const {
    writer << buffer_.size();
    writer.Write(buffer_.data(), buffer_.size());
}

bool Parser::LoadFromSnapshot(archive::IReader& reader) {
    const auto size = ReadValue<size_t>(reader);
    if (!size.ok()) {
        return false;
    }

    buffer_.resize(*size);
    return reader.Read(buffer_.data(), buffer_.size()).ok();
}

void SendAsync(const void* data, const size_t size, cable::ISocket& dst) {
    uint8_t size_bytes[kSizeSize];
    EncodeRequestSize(size, size_bytes);
    dst.SendAsync(size_bytes, sizeof(size_bytes));
    dst.SendAsync(data, size);
}

std::string EncodeQemudPacket(const std::string_view data) {
    std::string result;
    result.resize(kSizeSize + data.size());
    EncodeRequestSize(data.size(), reinterpret_cast<uint8_t*>(result.data()));
    std::memcpy(result.data() + kSizeSize, data.data(), data.size());
    return result;
}
}  // namespace goldfish::devices::qemud
