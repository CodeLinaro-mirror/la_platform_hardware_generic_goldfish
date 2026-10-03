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
#include "emulator/plugin/netsim/packet_transport.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/file/file.h"
#include "goldfish/qemu/qemubh.h"
#include "h4_parser.h"

namespace goldfish::netsim {

namespace {

class Protocol {
  public:
    virtual ~Protocol() = default;
    virtual ChipConfig chip_info() = 0;
    virtual void reset() = 0;
    virtual void reset_guest(Chardev* c) = 0;

    virtual void netsim_to_guest_packet(Chardev* c, std::unique_ptr<Packet> packet) = 0;

    virtual uint64_t guest_to_netsim_parser_bytes_requested() = 0;
    virtual void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) = 0;
};

class UwbProtocol : public Protocol {
  public:
    explicit UwbProtocol(std::vector<Packet>* output_queue) : mPacketQueue(output_queue) {}

    void reset() override {
        state_ = UCI_HEADER;
        mBytesWanted = UCI_HEADER_SIZE;
        mPacket.clear();
    }

    void reset_guest(Chardev* c) override {
        qemu_chr_be_write(c, reset_sequence, sizeof(reset_sequence));
    }

    void netsim_to_guest_packet(Chardev* c, std::unique_ptr<Packet> packet) override {
        // From netsim -> guest
        VLOG(2) << "NETSIM UWB: send (netsim -> guest)";
        qemu_chr_be_write(c, packet->data(), packet->size());
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
                [[fallthrough]];
            case UCI_PAYLOAD:
                mPacketQueue->push_back(std::move(mPacket));
                mPacket.clear();
                mBytesWanted = UCI_HEADER_SIZE;
                state_ = UCI_HEADER;
                break;
            }
        }
    }

    ChipConfig chip_info() override { return ChipConfig{.kind = ChipKind::kUwb}; }

  private:
    // UCI CORE_DEVICE_STATUS_NTF (DEVICE_STATE_ERROR) to trigger guest UWB stack recovery.
    static constexpr uint8_t reset_sequence[] = {0x60, 0x01, 0x00, 0x01, 0xff};

    const size_t UCI_HEADER_SIZE = 4;
    const size_t UCI_PAYLOAD_LENGTH_FIELD = 3;
    enum State { UCI_HEADER, UCI_PAYLOAD };

    State state_{UCI_HEADER};
    size_t mBytesWanted{UCI_HEADER_SIZE};
    std::vector<uint8_t> mPacket;
    std::vector<Packet>* mPacketQueue;
};

class NfcProtocol : public Protocol {
  public:
    explicit NfcProtocol(std::vector<Packet>* output_queue) : mPacketQueue(output_queue) {}

    void reset() override {
        state_ = NCI_HEADER;
        mBytesWanted = NCI_HEADER_SIZE;
        mPacket.clear();
    }

    void reset_guest(Chardev* c) override {
        qemu_chr_be_write(c, reset_sequence, sizeof(reset_sequence));
    }

    void netsim_to_guest_packet(Chardev* c, std::unique_ptr<Packet> packet) override {
        // From netsim -> guest
        VLOG(2) << "NETSIM NFC: send (netsim -> guest)";
        qemu_chr_be_write(c, packet->data(), packet->size());
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
                mPacketQueue->push_back(std::move(mPacket));
                mPacket.clear();
                mBytesWanted = NCI_HEADER_SIZE;
                state_ = NCI_HEADER;
                break;
            }
            }
        }
    }

    ChipConfig chip_info() override { return ChipConfig{.kind = ChipKind::kNfc}; }

  private:
    // NCI 2.0 CORE_RESET_NTF (UnrecoverableError) to trigger guest NFC stack recovery.
    static constexpr uint8_t reset_sequence[] = {0x60, 0x00, 0x05, 0x00, 0x01, 0x20, 0x00, 0x00};

    static constexpr size_t NCI_HEADER_SIZE = 3;
    static constexpr size_t NCI_PAYLOAD_LENGTH_FIELD = 2;
    enum State { NCI_HEADER, NCI_PAYLOAD };

    State state_{NCI_HEADER};
    size_t mBytesWanted{NCI_HEADER_SIZE};
    std::vector<uint8_t> mPacket;
    std::vector<Packet>* mPacketQueue;
};

class BtProtocol : public Protocol {
  public:
    explicit BtProtocol(std::vector<Packet>* output_queue) {
        auto enqueue = [queue = output_queue](uint8_t type, const std::vector<uint8_t>& data) {
            Packet packet;
            packet.reserve(1 + data.size());
            packet.push_back(type);
            packet.insert(packet.end(), data.begin(), data.end());
            queue->push_back(std::move(packet));
        };

        // TODO(b/548033766): Switch to virtio-bt (requires guest change).
        h4_parser_ = std::make_unique<rootcanal::H4Parser>(
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(static_cast<uint8_t>(rootcanal::PacketType::COMMAND), data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(static_cast<uint8_t>(rootcanal::PacketType::EVENT), data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(static_cast<uint8_t>(rootcanal::PacketType::ACL), data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(static_cast<uint8_t>(rootcanal::PacketType::SCO), data);
                },
                [enqueue](const std::vector<uint8_t>& data) {
                    enqueue(static_cast<uint8_t>(rootcanal::PacketType::ISO), data);
                },
                /*enable_recovery_state=*/true);
    }

    void reset() override { h4_parser_->Reset(); }

    void reset_guest(Chardev* c) override {
        qemu_chr_be_write(c, (uint8_t*)reset_sequence, sizeof(reset_sequence));
    }

    void netsim_to_guest_packet(Chardev* c, std::unique_ptr<Packet> packet) override {
        // From netsim -> guest
        VLOG(2) << "NETSIM BT: send (netsim -> guest)";
        qemu_chr_be_write(c, packet->data(), packet->size());
    }

    uint64_t guest_to_netsim_parser_bytes_requested() override {
        return h4_parser_->BytesRequested();
    }

    void guest_to_netsim_parser_consume(const uint8_t* buf, uint64_t len) override {
        h4_parser_->Consume(buf, len);
    }

    ChipConfig chip_info() override { return ChipConfig{.kind = ChipKind::kBluetooth}; }

  private:
    // This is a HCI Hardware Error Event, which indicates a serious problem
    // with the Bluetooth hardware. As a result, the Bluetooth stack should
    // crash and restart.
    static const uint8_t constexpr reset_sequence[] = {0x04, 0x10, 0x01, 0x42};

    std::unique_ptr<rootcanal::H4Parser> h4_parser_;
};

class CellularProtocol : public Protocol {
  public:
    explicit CellularProtocol(std::vector<Packet>& output_queue) : packet_queue_(output_queue) {}

    void reset() override { buffer_.clear(); }
    void reset_guest(Chardev* c) override {
        // Report radio off to the guest so it issues AT+CFUN=1.
        // Snapshots see modem restart and get a time update.
        const std::string reset_sequence = "+CFUN: 0\r\n";
        qemu_chr_be_write(c, reinterpret_cast<const uint8_t*>(reset_sequence.data()),
                          reset_sequence.size());
    }

    void netsim_to_guest_packet(Chardev* c, std::unique_ptr<Packet> packet) override {
        // From netsim -> guest
        VLOG(2) << "NETSIM CELL: send (netsim -> guest): "
                << std::string(packet->begin(), packet->end());
        qemu_chr_be_write(c, packet->data(), packet->size());
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
                    VLOG(2) << "NETSIM CELL: recv (guest -> netsim): "
                            << std::string(buffer_.begin(), buffer_.end());
                    packet_queue_.push_back(std::move(buffer_));
                    buffer_.clear();
                }
            }
        }
    }

    ChipConfig chip_info() override {
        ChipConfig chip{.kind = ChipKind::kCellular};

        auto& props = goldfish::avd::GetAvd().Props();
        if (!props.icc_profile.empty()) {
            auto content_or = android::base::file::read_whole_file(props.icc_profile, false);
            if (content_or.ok()) {
                chip.sim_profile = *content_or;
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
    std::vector<Packet>& packet_queue_;
};

void netsim_chardev_bh(void* obj);

struct NetsimChardevState {
    template <typename ProtocolFactory>
    NetsimChardevState(Object* obj, ProtocolFactory&& make_protocol)
            : incoming_bh(goldfish::qemu::MakeQemuBh(&netsim_chardev_bh, obj))
            , protocol(make_protocol(parser_packet_queue)) {}

    const goldfish::qemu::QEMUBHPtr incoming_bh;
    std::vector<Packet> parser_packet_queue;
    const std::unique_ptr<Protocol> protocol;
    std::unique_ptr<PacketTransport> transport;
    std::unique_ptr<Packet> incoming_packet;
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
    // Drop any guest writes that arrive before netsim_chardev_set_fe_open
    // connects the transport and resets the guest.
    if (!state->transport) {
        return len;
    }

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
    state->transport = CreatePacketTransport([nc](Packet& packet) {
        if (nc->state->incoming_packet) {
            // This shouldn't happen because we always return false from this lambda and
            // only call next_recv() once the last incoming_packet has been cleared.
            DCHECK(false) << "A new packet arrived from Netsim while the previous "
                             "packet is still pending delivery on QEMU's main loop. "
                             "Dropping the new packet to preserve flow control.";
            return false;
        }
        nc->state->incoming_packet = std::make_unique<Packet>(std::move(packet));
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
        state->transport.reset();
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
    if (nc->state->transport) {
        nc->state->transport->next_recv();
    }
}

bool netsim_chardev_open(Chardev* chr, ChardevBackend* backend, Error** errp) {
    VLOG(1) << "Realizing netsim chardev: " << chr->label;

    // Note that we don't initialize the connection to Netsimd here.
    // This is because chardevs are opened way before "device"s and so no AVD information is yet
    // available. However, the frontend is also a device and ordered after the device. So we connect
    // to Netsimd at that point (netsim_chardev_set_fe_open).
    // Report the backend as open immediately so the guest virtio-serial port stays
    // connected across snapshot load instead of seeing a transient EOF before
    // netsim_chardev_set_fe_open connects and resets the guest.
    qemu_chr_be_event(chr, CHR_EVENT_OPENED);
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
