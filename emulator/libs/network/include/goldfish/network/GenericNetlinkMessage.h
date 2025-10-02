// Copyright 2025 The Android Open Source Project
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

#include <stdint.h>
#include <vector>

#include "IOVector.h"

namespace goldfish::network {

struct nlmsghdr {
    uint32_t nlmsg_len;   /* Length of message including header */
    uint16_t nlmsg_type;  /* Message content */
    uint16_t nlmsg_flags; /* Additional flags */
    uint32_t nlmsg_seq;   /* Sequence number */
    uint32_t nlmsg_pid;   /* Sending process port ID */
} __attribute__((packed));

struct genlmsghdr {
    uint8_t cmd;
    uint8_t version;
    uint16_t reserved;
} __attribute__((packed));

/*  The generic netlink message format and payload is encoded as attributes.
 *
 *  +--------+---+----------+---+--------+-----+--------+---------+---+--------+---------+---+
 *  |nlmsghdr|pad|genlmsghdr|pad|userhdr | pad | nlattr | payload |pad| nlattr | payload |pad|
 *  +--------+---+----------+---+--------+-----+--------+---------+---+--------+---------+---+
 *                                             ^
 *               userData-----------------------
 *
 */
class GenericNetlinkMessage {
public:
    class Builder;
    GenericNetlinkMessage(uint32_t port,
                          uint32_t seq,
                          int family,
                          int hdrlen,
                          int flags,
                          uint8_t cmd,
                          uint8_t version);

    GenericNetlinkMessage(const uint8_t* data, size_t size, int hdrlen = 0);
    GenericNetlinkMessage(IOVector iovec, int hdrlen = 0);

    // No copy.
    GenericNetlinkMessage(const GenericNetlinkMessage&) = delete;
    GenericNetlinkMessage& operator=(const GenericNetlinkMessage&) = delete;
    GenericNetlinkMessage(GenericNetlinkMessage&&) = default;
    GenericNetlinkMessage& operator=(GenericNetlinkMessage&&) = default;
    ~GenericNetlinkMessage() = default;

    // will not search in nested attribute
    struct iovec getAttribute(int attributeId) const;
    bool getAttribute(int attributeId, void* dst, size_t size) const;
    // If attributeId already exists, this method will override the original
    // value
    bool putAttribute(int attributeId, const void* src, size_t size);
    struct nlmsghdr* netlinkHeader();
    const struct nlmsghdr* netlinkHeader() const;
    struct genlmsghdr* genericNetlinkHeader();
    const struct genlmsghdr* genericNetlinkHeader() const;
    uint8_t* userHeader();
    const uint8_t* userHeader() const;
    uint8_t* userData();
    const uint8_t* userData() const;
    size_t userDataLen() const;
    uint8_t* data();
    const uint8_t* data() const;
    size_t dataLen() const;
    bool isValid() const;

    static constexpr int NL_AUTO_SEQ = 0;
    static constexpr int NL_AUTO_PORT = 0;
    static constexpr int NLMSG_MIN_TYPE = 0x10;

private:
    void putHeader(uint32_t pid, uint32_t seq, int type, int flags);
    void resizeByHeaderLength(size_t size);

    std::vector<uint8_t> mData;
    size_t mUserHeaderLen;
};

}  // namespace goldfish::network
