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

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
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
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/file/file.h"
#include "goldfish/qemu/qemubh.h"
#include "h4_parser.h"
#include "netsim_transport.h"

namespace goldfish::netsim {

namespace {

class Protocol {
  public:
    virtual ~Protocol() = default;
    virtual ::netsim::startup::Chip chip_info() = 0;
    virtual void reset() = 0;
    virtual void reset_guest(Chardev* c) = 0;

    virtual void netsim_to_guest_packet(
            Chardev* c, std::unique_ptr<::netsim::packet::PacketResponse> packet) = 0;

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

    void netsim_to_guest_packet(Chardev* c,
                                std::unique_ptr<::netsim::packet::PacketResponse> packet) override {
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

class NfcProtocol : public Protocol {
  public:
    explicit NfcProtocol(std::vector<::netsim::packet::PacketRequest>* output_queue)
            : mPacketQueue(output_queue) {}

    void reset() override {
        state_ = NCI_HEADER;
        mBytesWanted = NCI_HEADER_SIZE;
        mPacket.clear();
    }

    void reset_guest(Chardev* /*c*/) override {
        // N/A
    }

    void netsim_to_guest_packet(Chardev* c,
                                std::unique_ptr<::netsim::packet::PacketResponse> packet) override {
        if (packet->has_packet()) {
            // From netsim -> guest
            VLOG(2) << "NETSIM NFC: send (netsim -> guest)";
            qemu_chr_be_write(c, reinterpret_cast<const uint8_t*>(packet->packet().data()),
                              packet->packet().size());
        } else {
            LOG(WARNING) << "Unexpected packet " << packet->DebugString();
        }
    }

    uint64_t guest_to_netsim_parser_bytes_requested() override { return mBytesWanted; }

    void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) override {
        if (len <= 0) {
            LOG(INFO) << "NFC remote disconnected gracefully (received 0 bytes); NFC emulation is "
                         "disabled.";
            return;
        }
        if (len > mBytesWanted) {
            LOG(FATAL) << "NFC: More bytes read than expected";
        }

        mPacket.insert(mPacket.end(), buf, buf + len);
        mBytesWanted -= len;

        if (mBytesWanted == 0) {
            switch (state_) {
            case NCI_HEADER:
                mBytesWanted = mPacket[NCI_PAYLOAD_LENGTH_FIELD];
                state_ = NCI_PAYLOAD;
                if (mBytesWanted > 0) {
                    break;
                }
                // Fall through if entire packet is just a header (payload length is 0)
                [[fallthrough]];
            case NCI_PAYLOAD: {
                ::netsim::packet::PacketRequest request;
                request.set_allocated_packet(new std::string(mPacket.begin(), mPacket.end()));
                mPacketQueue->push_back(std::move(request));
                mPacket.clear();
                mBytesWanted = NCI_HEADER_SIZE;
                state_ = NCI_HEADER;
                break;
            }
            }
        }
    }

    ::netsim::startup::Chip chip_info() override {
        ::netsim::startup::Chip chip;
        chip.set_kind(::netsim::common::ChipKind::NFC);
        return chip;
    }

  private:
    static constexpr size_t NCI_HEADER_SIZE = 3;
    static constexpr size_t NCI_PAYLOAD_LENGTH_FIELD = 2;
    enum State { NCI_HEADER, NCI_PAYLOAD };

    State state_{NCI_HEADER};
    size_t mBytesWanted{NCI_HEADER_SIZE};
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

        // TODO(b/548033766): Switch to virtio-bt (requires guest change).
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
                },
                /*enable_recovery_state=*/true);
    }

    void reset() override { h4_parser_->Reset(); }

    void reset_guest(Chardev* c) override {
        qemu_chr_be_write(c, (uint8_t*)reset_sequence, sizeof(reset_sequence));
    }

    void netsim_to_guest_packet(Chardev* c,
                                std::unique_ptr<::netsim::packet::PacketResponse> packet) override {
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

class CellularProtocol : public Protocol {
  public:
    CellularProtocol(std::vector<::netsim::packet::PacketRequest>& output_queue)
            : packet_queue_(output_queue) {}

    void reset() override { buffer_.clear(); }
    void reset_guest(Chardev* c) override {
        // N/A
    }

    void netsim_to_guest_packet(Chardev* c,
                                std::unique_ptr<::netsim::packet::PacketResponse> packet) override {
        if (packet->has_packet()) {
            // From netsim -> guest
            VLOG(2) << "NETSIM CELL: send (netsim -> guest): " << packet->packet();
            qemu_chr_be_write(c, (const uint8_t*)packet->packet().data(), packet->packet().size());
        } else {
            LOG(WARNING) << "Unexpected packet " << packet->DebugString();
        }
    }

    uint64_t guest_to_netsim_parser_bytes_requested() override {
        // AT commands are stream-based and do not encode a packet length in a header.
        return 1024;
    }

    void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) override {
        for (uint64_t i = 0; i < len; ++i) {
            uint8_t c = buf[i];
            buffer_.push_back(c);
            if (c == '\r') {
                // Peek next byte to see if it is \n to keep \r\n together
                if (i + 1 < len && buf[i + 1] == '\n') {
                    buffer_.push_back('\n');
                    i++;  // Consume \n
                }
            }
            if (c == '\r' || c == '\n' || c == 0x1A || c == 0x1B) {
                if (!buffer_.empty()) {
                    ::netsim::packet::PacketRequest request;
                    request.set_allocated_packet(new std::string(buffer_.begin(), buffer_.end()));
                    VLOG(2) << "NETSIM CELL: recv (guest -> netsim): " << request.packet();
                    packet_queue_.push_back(std::move(request));
                    buffer_.clear();
                }
            }
        }
    }

    ::netsim::startup::Chip chip_info() override {
        ::netsim::startup::Chip chip;
        chip.set_kind(::netsim::common::ChipKind::CELLULAR);

        auto& props = goldfish::avd_info::GetAvd().Props();
        if (!props.icc_profile.empty()) {
            auto content_or = android::base::file::read_whole_file(props.icc_profile, false);
            if (content_or.ok()) {
                chip.set_sim_profile(*content_or);
                VLOG(1) << "NETSIM CELL: Loaded custom SIM profile from " << props.icc_profile;
            } else {
                LOG(ERROR) << "NETSIM CELL: Failed to read custom SIM profile at "
                           << props.icc_profile << ": " << content_or.status();
            }
        }
        return chip;
    }

  private:
    std::vector<uint8_t> buffer_;
    std::vector<::netsim::packet::PacketRequest>& packet_queue_;
};

void netsim_chardev_bh(void* obj);

struct NetsimChardevState {
    template <typename ProtocolFactory>
    NetsimChardevState(Object* obj, ProtocolFactory&& make_protocol)
            : incoming_bh(goldfish::qemu::MakeQemuBh(&netsim_chardev_bh, obj))
            , protocol(make_protocol(parser_packet_queue)) {}

    const goldfish::qemu::QEMUBHPtr incoming_bh;
    std::vector<::netsim::packet::PacketRequest> parser_packet_queue;
    const std::unique_ptr<Protocol> protocol;
    std::unique_ptr<NetsimTransport> transport;
    std::unique_ptr<::netsim::packet::PacketResponse> incoming_packet;
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
#define TYPE_NETSIM_CHARDEV_NFC "chardev-netsim-nfc"
#define TYPE_NETSIM_CHARDEV_CELLULAR "chardev-netsim-cellular"

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
    auto* nc = NETSIM_CHARDEV(chr);
    auto* state = nc->state;
    if (state->transport) {
        if (fe_open) {
            VLOG(1) << "NETSIM chardev: transport already exists, destroying old one first: "
                    << chr->label;
        } else {
            VLOG(1) << "NETSIM chardev: frontend disconnected: " << chr->label;
        }
        state->transport.reset();
    }

    if (!fe_open) {
        qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
        return;
    }

    VLOG(1) << "NETSIM chardev: frontend connected, sending reset: " << chr->label;
    state->transport =
            std::make_unique<NetsimTransport>([nc](::netsim::packet::PacketResponse* packet) {
                if (nc->state->incoming_packet) {
                    // This shouldn't happen because we always return false from this lambda and
                    // only call next_recv() once the last incoming_packet has been cleared.
                    DCHECK(false) << "A new packet arrived from Netsim while the previous "
                                     "packet is still pending delivery on QEMU's main loop. "
                                     "Dropping the new packet to preserve flow control.";
                    return false;
                }
                nc->state->incoming_packet =
                        std::make_unique<::netsim::packet::PacketResponse>(std::move(*packet));
                if (nc->state->incoming_bh) {
                    qemu_bh_schedule(nc->state->incoming_bh.get());
                }
                return false;
            });

    // TODO maybe connect in background
    // TODO maybe reconnect - ondone callback and then call initialize again (also send reset to
    // guest).
    state->protocol->reset();

    if (auto status = state->transport->initialize(state->protocol->chip_info()); !status.ok()) {
        error_printf("failed to initialize netsim transport %s: %s", chr->label,
                     status.ToString().c_str());
        return;
    }

    qemu_chr_be_event(chr, CHR_EVENT_OPENED);

    // Send reset sequence to guest.
    state->protocol->reset_guest(chr);
}

void netsim_chardev_bh(void* obj) {
    auto* nc = NETSIM_CHARDEV(obj);
    if (!nc->state->incoming_packet) {
        DCHECK(false)
                << "Netsim chardev bottom half execution was scheduled on QEMU's main loop, but no "
                   "incoming packet buffer was found. Skipping packet transmission.";
        return;
    }
    auto* chr = CHARDEV(obj);
    nc->state->protocol->netsim_to_guest_packet(chr, std::move(nc->state->incoming_packet));
    nc->state->transport->next_recv();
}

bool netsim_chardev_open(Chardev* chr, ChardevBackend* backend, Error** errp) {
    VLOG(1) << "Realizing netsim chardev: " << chr->label;

    // Note that we don't initialize the connection to Netsimd here.
    // This is because chardevs are opened way before "device"s and so no AVD information is yet
    // available. However, the frontend is also a device and ordered after the device. So we connect
    // to Netsimd at that point (netsim_chardev_set_fe_open).
    return true;
}

void netsim_chardev_bt_instance_init(Object* obj) {
    VLOG(1) << "NETSIM BT init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState(
            obj, [](auto& queue) { return std::make_unique<BtProtocol>(&queue); });
}

void netsim_chardev_uwb_instance_init(Object* obj) {
    VLOG(1) << "NETSIM UWB init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState(
            obj, [](auto& queue) { return std::make_unique<UwbProtocol>(&queue); });
}

void netsim_chardev_nfc_instance_init(Object* obj) {
    VLOG(1) << "NETSIM NFC init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState(
            obj, [](auto& queue) { return std::make_unique<NfcProtocol>(&queue); });
}

void netsim_chardev_cellular_instance_init(Object* obj) {
    VLOG(1) << "NETSIM CELLULAR init";
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    nc->state = new NetsimChardevState(
            obj, [](auto& queue) { return std::make_unique<CellularProtocol>(queue); });
}
void netsim_chardev_instance_finalize(Object* obj) {
    NetsimChardev* nc = NETSIM_CHARDEV(obj);
    delete nc->state;
}

void netsim_chardev_class_init(ObjectClass* oc, const void* data) {
    ChardevClass* cc = CHARDEV_CLASS(oc);
    cc->chr_open = netsim_chardev_open;
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

const TypeInfo netsim_chardev_nfc_type_info = {
    .name = TYPE_NETSIM_CHARDEV_NFC,
    .parent = TYPE_NETSIM_CHARDEV,
    .instance_size = sizeof(NetsimChardev),
    .instance_init = netsim_chardev_nfc_instance_init,
};

const TypeInfo netsim_chardev_cellular_type_info = {
    .name = TYPE_NETSIM_CHARDEV_CELLULAR,
    .parent = TYPE_NETSIM_CHARDEV,
    .instance_size = sizeof(NetsimChardev),
    .instance_init = netsim_chardev_cellular_instance_init,
};

}  // namespace

void netsim_chardev_register_types(void) {
    type_register_static(&netsim_chardev_type_info);
    type_register_static(&netsim_chardev_bt_type_info);
    type_register_static(&netsim_chardev_uwb_type_info);
    type_register_static(&netsim_chardev_nfc_type_info);
    type_register_static(&netsim_chardev_cellular_type_info);
}

}  // namespace goldfish::netsim
