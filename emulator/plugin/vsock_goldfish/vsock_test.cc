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

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/vsock/clear.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/vsock_low_level.h"

struct QEMUFile {
    std::string buffer;
    size_t read_offset = 0;
};

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_vsock.h"

size_t qemu_get_buffer(QEMUFile* f, uint8_t* buf, size_t size) {
    if (f == nullptr || f->read_offset >= f->buffer.size()) {
        return 0;
    }
    const size_t avail = std::min(size, f->buffer.size() - f->read_offset);
    std::memcpy(buf, f->buffer.data() + f->read_offset, avail);
    f->read_offset += avail;
    return avail;
}

void qemu_put_buffer(QEMUFile* f, const uint8_t* buf, size_t size) {
    if (f != nullptr && buf != nullptr && size > 0) {
        f->buffer.append(reinterpret_cast<const char*>(buf), size);
    }
}
}
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::vsock {
namespace {

using devices::cable::IPlug;
using devices::cable::PlugOrSocket;
using devices::cable::PlugPtr;
using devices::cable::RegisterPlugLoader;
using devices::cable::SocketPtr;

constexpr uint32_t kGuestPort = 5555;
constexpr uint64_t kGuestCid = 3;
constexpr uint64_t kHostCid = 2;

struct CapturedPacket {
    struct virtio_vsock_hdr hdr;
    std::string payload;
};

class SnapshottablePlug : public IPlug {
  public:
    static constexpr const char* kTypeId = "vsock_test_snapshottable_plug";

    explicit SnapshottablePlug(uint32_t tag) : tag_(tag) {}

    void SetSocket(SocketPtr socket) { socket_ = std::move(socket); }
    devices::cable::ISocket* socket() const { return socket_.get(); }
    uint32_t tag() const { return tag_; }
    const std::string& received() const { return received_; }

    void OnConnect() override {}

    SocketPtr OnUnplug() override { return std::move(socket_); }

    bool OnReceive(const void* data, size_t size) override {
        received_.append(static_cast<const char*>(data), size);
        return true;
    }

    bool SupportsLoadingFromSnapshot() const override { return true; }

    TypeId GetSnapshotTypeId() const override { return kTypeId; }

    bool SaveStateToSnapshot(archive::IWriter& writer) const override {
        writer << tag_;
        return true;
    }

    static std::unordered_map<uint32_t, std::shared_ptr<SnapshottablePlug>>& RestoredPlugs() {
        static auto* restored =
                new std::unordered_map<uint32_t, std::shared_ptr<SnapshottablePlug>>();
        return *restored;
    }

    static void EnsureLoaderRegistered() {
        static const bool kRegistered = RegisterPlugLoader(
                kTypeId, [](SocketPtr socket, archive::IReader& reader) -> PlugOrSocket {
                    uint32_t tag = 0;
                    if (!archive::ReadValue(reader, tag).ok()) {
                        return std::move(socket);
                    }
                    auto plug = std::make_shared<SnapshottablePlug>(tag);
                    plug->SetSocket(std::move(socket));
                    RestoredPlugs()[tag] = plug;
                    return PlugPtr(plug);
                });
        (void)kRegistered;
    }

  private:
    uint32_t tag_ = 0;
    SocketPtr socket_;
    std::string received_;
};

class VsockSnapshotFlagsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        SnapshottablePlug::EnsureLoaderRegistered();
        SnapshottablePlug::RestoredPlugs().clear();
        static const GoldfishVirtIOVSockDevAPI kApi = {
            .haveHostToGuestPackets = &VsockSnapshotFlagsTest::OnHaveHostToGuestPackets,
            .sendPacketHostToGuest = &VsockSnapshotFlagsTest::OnSendPacketHostToGuest,
            .haveHostToGuestEvents = &VsockSnapshotFlagsTest::OnHaveHostToGuestEvents,
            .sendEventHostToGuest = &VsockSnapshotFlagsTest::OnSendEventHostToGuest,
        };
        impl_ = goldfish_virtio_vsock_impl_realize(this, &kApi);
    }

    void TearDown() override {
        SnapshottablePlug::RestoredPlugs().clear();
        clear();
        goldfish_virtio_vsock_impl_unrealize(impl_);
    }

    uint32_t CompleteHandshake(uint32_t guest_port, uint32_t guest_buf_alloc) {
        uint32_t host_port = 0;
        for (auto it = packets_.rbegin(); it != packets_.rend(); ++it) {
            if (it->hdr.op == VIRTIO_VSOCK_OP_REQUEST && it->hdr.dst_port == guest_port) {
                host_port = it->hdr.src_port;
                break;
            }
        }
        EXPECT_NE(host_port, 0U);

        struct virtio_vsock_hdr resp = {
            .src_cid = kGuestCid,
            .dst_cid = kHostCid,
            .src_port = guest_port,
            .dst_port = host_port,
            .len = 0,
            .type = VIRTIO_VSOCK_TYPE_STREAM,
            .op = VIRTIO_VSOCK_OP_RESPONSE,
            .flags = 0,
            .buf_alloc = guest_buf_alloc,
            .fwd_cnt = 0,
        };
        goldfish_virtio_vsock_accept_guest_to_host_control(impl_, &resp);
        return host_port;
    }

    static void OnHaveHostToGuestPackets(void* opaque) {
        static_cast<VsockSnapshotFlagsTest*>(opaque)->notify_count_++;
    }

    static VirtIOVSockSendResult OnSendPacketHostToGuest(void* opaque, struct virtio_vsock_hdr* hdr,
                                                         const void* data) {
        auto* self = static_cast<VsockSnapshotFlagsTest*>(opaque);
        if (self->vq_full_) {
            return MakeVirtIOVSockSendResult(/*is_vq_full=*/true, /*need_notify=*/false, 0);
        }
        const size_t len = hdr->len;
        CapturedPacket pkt;
        pkt.hdr = *hdr;
        if (data != nullptr && len > 0) {
            pkt.payload.assign(static_cast<const char*>(data), len);
        }
        self->packets_.push_back(std::move(pkt));
        return MakeVirtIOVSockSendResult(/*is_vq_full=*/false, /*need_notify=*/true, len);
    }

    static void OnHaveHostToGuestEvents(void* /*opaque*/) {}

    static VirtIOVSockSendResult OnSendEventHostToGuest(void* /*opaque*/,
                                                        const struct virtio_vsock_event* /*evt*/) {
        return MakeVirtIOVSockSendResult(false, false, 1);
    }

    void* impl_ = nullptr;
    bool vq_full_ = false;
    size_t notify_count_ = 0;
    std::vector<CapturedPacket> packets_;
};

TEST_F(VsockSnapshotFlagsTest, HighBitSendOpMaskDoesNotCorruptSnapshotStreamViaVarint) {
    auto plug = std::make_shared<SnapshottablePlug>(/*tag=*/101);
    SocketPtr sock = Connect(kGuestPort, plug);
    const uint32_t host_port = CompleteHandshake(kGuestPort, /*guest_buf_alloc=*/0);
    plug->SetSocket(std::move(sock));

    // With guest_buf_alloc == 0 and the virtqueue full, SendAsync() queues
    // "hello" in hostToGuestBuf and sendPacketsLocked() sets bit 7
    // (1U << VIRTIO_VSOCK_OP_CREDIT_REQUEST == 0x80) in stream.sendOpMask.
    // Combined with isConnected (0x01), flags == 0x81 >= 128.
    // If saveToSnapshot() serializes flags as `unsigned` (7-bit varint, 2 bytes:
    // 0x81 0x01) while loadFromSnapshot() reads `uint8_t` (1 raw byte: 0x81),
    // the leftover 0x01 varint byte is read by hostToGuestBuf.LoadFromSnapshot()
    // as new_size == 1, corrupting hostToGuestBuf and desynchronizing
    // LoadPlugFromSnapshot().
    vq_full_ = true;
    const std::string kPayload = "hello";
    plug->socket()->SendAsync(kPayload.data(), kPayload.size());

    QEMUFile snapshot;
    ASSERT_EQ(goldfish_virtio_vsock_impl_save(impl_, &snapshot), 0);

    vq_full_ = false;
    packets_.clear();
    (void)goldfish_virtio_vsock_impl_load(impl_, &snapshot);

    ASSERT_EQ(SnapshottablePlug::RestoredPlugs().size(), 1U);
    ASSERT_TRUE(SnapshottablePlug::RestoredPlugs().count(101));

    // Grant guest credit and verify the buffered payload is transmitted intact
    // without a phantom varint byte.
    struct virtio_vsock_hdr credit = {
        .src_cid = kGuestCid,
        .dst_cid = kHostCid,
        .src_port = kGuestPort,
        .dst_port = host_port,
        .len = 0,
        .type = VIRTIO_VSOCK_TYPE_STREAM,
        .op = VIRTIO_VSOCK_OP_CREDIT_UPDATE,
        .flags = 0,
        .buf_alloc = 65536,
        .fwd_cnt = 0,
    };
    goldfish_virtio_vsock_accept_guest_to_host_control(impl_, &credit);
    goldfish_virtio_vsock_handle_host_to_guest(impl_);

    std::string transmitted;
    for (const CapturedPacket& pkt : packets_) {
        if (pkt.hdr.op == VIRTIO_VSOCK_OP_RW) {
            transmitted.append(pkt.payload);
        }
    }
    EXPECT_EQ(transmitted, kPayload);
}

}  // namespace
}  // namespace goldfish::vsock
