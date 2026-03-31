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

#include <cstdint>

#include "absl/log/log.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "chardev/char.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qom/object.h"
// IWYU pragma: end_keep
// clang-format on
}

#undef send
#include "android/emulation/control/enum_translate.h"
#include "netsim_transport.h"
#include "h4_parser.h"

namespace goldfish::netsim {

namespace {

class Protocol {
  public:
    virtual ~Protocol() = default;
    virtual ::netsim::startup::Chip chip_info() = 0;
    virtual void reset() = 0;
    virtual void reset_guest(Chardev* c) = 0;

    virtual void netsim_to_guest_packet(Chardev* c, ::netsim::packet::PacketResponse* packet) = 0;

    virtual uint64_t guest_to_netsim_parser_bytes_requested() = 0;
    virtual void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) = 0;
};

class UwbProtocol : public Protocol {
  public:
    UwbProtocol(std::vector<::netsim::packet::PacketRequest>* output_queue)
            : mPacketQueue(output_queue) {}

    void reset() override {
        state_ = UCI_HEADER;
        mBytesWanted = UCI_HEADER_SIZE;
    }

    void reset_guest(Chardev* c) override {
        // N/A
    }

    void netsim_to_guest_packet(Chardev* c, ::netsim::packet::PacketResponse* packet) override {
        if (packet->has_packet()) {
            // From netsim -> guest
            VLOG(2) << "NETSIM UWB: send (netsim -> guest)";
            qemu_chr_be_write(c, (uint8_t*)packet->packet().data(), packet->packet().size());
        } else {
            LOG(WARNING) << "Unexpected packet " << packet->DebugString();
        }
    }

    uint64_t guest_to_netsim_parser_bytes_requested() override { return mBytesWanted; }

    void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) override {
        if (len <= 0) {
            LOG(INFO) << "remote disconnected, or unhandled error?";
            return;
        }
        size_t bytes_to_read = mBytesWanted;
        if ((uint32_t)len > bytes_to_read) {
            LOG(FATAL) << "Uwb: More bytes read than expected";
        }

        mPacket.insert(mPacket.end(), buf, buf + len);
        mBytesWanted -= len;

        if (mBytesWanted == 0) {
            switch (state_) {
            case UCI_HEADER:
                mBytesWanted = mPacket[UCI_PAYLOAD_LENGTH_FIELD];
                state_ = UCI_PAYLOAD;
                if (mBytesWanted > 0) {
                    break;
                }
                // Fall through if entire packet is just a header
            case UCI_PAYLOAD:
                ::netsim::packet::PacketRequest request;
                request.set_allocated_packet(new std::string(mPacket.begin(), mPacket.end()));
                mPacketQueue->push_back(request);
                mPacket.clear();
                mBytesWanted = UCI_HEADER_SIZE;
                state_ = UCI_HEADER;
                break;
            }
        }
    }

    ::netsim::startup::Chip chip_info() override {
        ::netsim::startup::Chip chip;
        chip.set_kind(::netsim::common::ChipKind::UWB);
        return chip;
    }

  private:
    const size_t UCI_HEADER_SIZE = 4;
    const size_t UCI_PAYLOAD_LENGTH_FIELD = 3;
    enum State { UCI_HEADER, UCI_PAYLOAD };

    State state_{UCI_HEADER};
    size_t mBytesWanted{UCI_HEADER_SIZE};
    std::vector<uint8_t> mPacket;
    std::vector<::netsim::packet::PacketRequest>* mPacketQueue;
};

class BtProtocol : public Protocol {
  public:
    BtProtocol(std::vector<::netsim::packet::PacketRequest>* output_queue) {
        auto enqueue = [queue = output_queue](::netsim::packet::HCIPacket::PacketType type,
                                              const std::vector<uint8_t>& data) {
            ::netsim::packet::PacketRequest request;
            auto* packet = request.mutable_hci_packet();
            packet->set_packet_type(type);
            packet->set_packet(std::string(data.begin(), data.end()));
            queue->push_back(request);
        };

        h4_parser_ = std::make_unique<rootcanal::H4Parser>(
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(::netsim::packet::HCIPacket::COMMAND, data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(::netsim::packet::HCIPacket::EVENT, data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(::netsim::packet::HCIPacket::ACL, data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(::netsim::packet::HCIPacket::SCO, data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(::netsim::packet::HCIPacket::ISO, data);
                });
    }

    void reset() override { h4_parser_->Reset(); }

    void reset_guest(Chardev* c) override {
        qemu_chr_be_write(c, (uint8_t*)reset_sequence, sizeof(reset_sequence));
    }

    void netsim_to_guest_packet(Chardev* c, ::netsim::packet::PacketResponse* packet) override {
        if (packet->has_hci_packet()) {
            // From netsim -> guest
            VLOG(2) << "NETSIM BT: send (netsim -> guest)";
            uint8_t type = static_cast<uint8_t>(
                    EnumTranslate::translate(kPacketType, packet->hci_packet().packet_type()));
            qemu_chr_be_write(c, &type, 1);
            qemu_chr_be_write(c, (uint8_t*)packet->hci_packet().packet().data(),
                              packet->hci_packet().packet().size());
        } else {
            LOG(WARNING) << "Unexpected packet " << packet->DebugString();
        }
    }

    uint64_t guest_to_netsim_parser_bytes_requested() override {
        return h4_parser_->BytesRequested();
    }

    void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) override {
        h4_parser_->Consume(buf, len);
    }

    ::netsim::startup::Chip chip_info() override {
        ::netsim::startup::Chip chip;
        chip.set_kind(::netsim::common::ChipKind::BLUETOOTH);
        // TODO if ("ro.build.version.sdk=33")
        // chip.mutable_bt_properties()->mutable_features()->set_le_connected_isochronous_stream(false);
        return chip;
    }

    // This is a HCI Hardware Error Event, which indicates a serious problem
    // with the Bluetooth hardware. As a result, the Bluetooth stack should
    // crash and restart.
    static const uint8_t constexpr reset_sequence[] = {0x04, 0x10, 0x01, 0x42};

    // External <-> Internal mapping. This makes sure the external gRPC
    // representation is independent of what is used internally.
    enum class PacketType : uint8_t {
        UNKNOWN = 0,
        COMMAND = 1,
        ACL = 2,
        SCO = 3,
        EVENT = 4,
        ISO = 5,
    };

    static constexpr const std::tuple<::netsim::packet::HCIPacket::PacketType, PacketType>
            kPacketType[] = {
                {::netsim::packet::HCIPacket::HCI_PACKET_UNSPECIFIED, PacketType::UNKNOWN},
                {::netsim::packet::HCIPacket::COMMAND, PacketType::COMMAND},
                {::netsim::packet::HCIPacket::ACL, PacketType::ACL},
                {::netsim::packet::HCIPacket::SCO, PacketType::SCO},
                {::netsim::packet::HCIPacket::EVENT, PacketType::EVENT},
                {::netsim::packet::HCIPacket::ISO, PacketType::ISO},
            };

    std::unique_ptr<rootcanal::H4Parser> h4_parser_;
};

struct NetsimChardevState {
    std::unique_ptr<NetsimTransport> transport;
    std::unique_ptr<Protocol> protocol;
    std::vector<::netsim::packet::PacketRequest> parser_packet_queue;
};

struct NetsimChardev {
    Chardev parent_class;
    NetsimChardevState* state;
};

// This type is a parent for the other two but shouldn't be instantiated directly.
#define TYPE_NETSIM_CHARDEV "chardev-netsim"
#define NETSIM_CHARDEV(obj) OBJECT_CHECK(NetsimChardev, (obj), TYPE_NETSIM_CHARDEV)

#define TYPE_NETSIM_CHARDEV_BT "chardev-netsim-bt"
#define TYPE_NETSIM_CHARDEV_UWB "chardev-netsim-uwb"

int netsim_chardev_write(Chardev* chr, const uint8_t* buf, int len) {
    // From guest -> netsim
    VLOG(2) << "NETSIM: receive (netsim <- guest)";
    auto* state = NETSIM_CHARDEV(chr)->state;

    // parser writes into parser_packet_queue
    uint64_t left = len;
    while (left > 0) {
        uint64_t send =
                std::min<uint64_t>(left, state->protocol->guest_to_netsim_parser_bytes_requested());
        state->protocol->guest_to_netsim_parser_consume(buf, send);
        buf += send;
        left -= send;
    }

    for (auto& packet : state->parser_packet_queue) {
        state->transport->send(std::move(packet));
    }
    state->parser_packet_queue.clear();

    return len;
}

void netsim_chardev_set_fe_open(Chardev* chr, int fe_open) {
    if (!fe_open) {
        return;
    }

    VLOG(1) << "NETSIM chardev: frontend connected, sending reset: " << chr->label;
    auto* state = NETSIM_CHARDEV(chr)->state;

    // TODO maybe connect in background
    // TODO maybe reconnect - ondone callback and then call initialize again (also send reset to
    // guest).
    state->protocol->reset();

    if (auto status = state->transport->initialize(state->protocol->chip_info()); !status.ok()) {
        error_printf("failed to initialize netsim transport %s: %s", chr->label,
                     status.ToString().c_str());
        return;
    }

    // Send reset sequence to guest.
    state->protocol->reset_guest(chr);
}

void netsim_chardev_open(Chardev* chr, ChardevBackend* backend, bool* be_opened, Error** errp) {
    VLOG(1) << "Realizing netsim chardev: " << chr->label;

    NetsimChardev* nc = NETSIM_CHARDEV(chr);
    nc->state->transport = std::make_unique<NetsimTransport>(
            [chr, protocol = nc->state->protocol.get()](::netsim::packet::PacketResponse* packet) {
                protocol->netsim_to_guest_packet(chr, packet);
                // Try to receive next packet immediately.
                return true;
            });

    // Note that we don't initialize the connection to Netsimd here.
    // This is because chardevs are opened way before "device"s and so no AVD information is yet
    // available. However, the frontend is also a device and ordered after the device. So we connect
    // to Netsimd at that point (netsim_chardev_set_fe_open).
}

void netsim_chardev_bt_instance_init(Object* obj) {
    VLOG(1) << "NETSIM BT init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState;
    nc->state->protocol = std::make_unique<BtProtocol>(&nc->state->parser_packet_queue);
}

void netsim_chardev_uwb_instance_init(Object* obj) {
    VLOG(1) << "NETSIM UWB init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState;
    nc->state->protocol = std::make_unique<UwbProtocol>(&nc->state->parser_packet_queue);
}

void netsim_chardev_instance_finalize(Object* obj) {
    NetsimChardev* nc = NETSIM_CHARDEV(obj);

    // This calls NetsimTransport's destructor, which calls cancel and await
    nc->state->transport.reset();
    delete nc->state;
}

void netsim_chardev_class_init(ObjectClass* oc, void* data) {
    ChardevClass* cc = CHARDEV_CLASS(oc);
    cc->open = netsim_chardev_open;
    cc->chr_write = netsim_chardev_write;
    cc->chr_set_fe_open = netsim_chardev_set_fe_open;
}

const TypeInfo netsim_chardev_type_info = {
    .name = TYPE_NETSIM_CHARDEV,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(NetsimChardev),
    .instance_finalize = netsim_chardev_instance_finalize,
    .class_init = netsim_chardev_class_init,
};

const TypeInfo netsim_chardev_bt_type_info = {
    .name = TYPE_NETSIM_CHARDEV_BT,
    .parent = TYPE_NETSIM_CHARDEV,
    .instance_size = sizeof(NetsimChardev),
    .instance_init = netsim_chardev_bt_instance_init,
};

const TypeInfo netsim_chardev_uwb_type_info = {
    .name = TYPE_NETSIM_CHARDEV_UWB,
    .parent = TYPE_NETSIM_CHARDEV,
    .instance_size = sizeof(NetsimChardev),
    .instance_init = netsim_chardev_uwb_instance_init,
};

}  // namespace

void netsim_chardev_register_types(void) {
    type_register_static(&netsim_chardev_type_info);
    type_register_static(&netsim_chardev_bt_type_info);
    type_register_static(&netsim_chardev_uwb_type_info);
}

}  // namespace goldfish::netsim
