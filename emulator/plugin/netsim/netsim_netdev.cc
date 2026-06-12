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

#include "goldfish/netsim/netsim_netdev.h"

#include <sys/types.h>

#include <memory>

#include "absl/log/log.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "hw/qdev-core.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
// IWYU pragma: end_keep
// clang-format on
}

#undef send

#include "goldfish/network/generic_netlink_message.h"
#include "netsim_transport.h"

#define __packed
typedef int8_t s8;
typedef uint8_t u8;
typedef uint16_t u16;
#include "standard-headers/linux/mac80211_hwsim.h"

namespace goldfish::netsim {

namespace {

struct NetsimNetdev {
    DeviceClass parent_class;
    ::netsim::common::ChipKind chip_kind;
};

#define TYPE_NETSIM_NETDEV "netsim-netdev"
#define NETSIM_NETDEV(obj) OBJECT_CHECK(NetsimNetdev, (obj), TYPE_NETSIM_NETDEV)
#define NETSIM_NETDEV_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(NetsimNetdev, obj, TYPE_NETSIM_NETDEV)

struct NetsimState {
    std::unique_ptr<NetsimTransport> transport;
    std::unique_ptr<std::vector<uint8_t>> async_tx;
};

struct NetsimNicState {
    // Note C struct so no c/d-tor.
    NetClientState nc;
    NetsimState* netsim;
    bool is_wifi;
};

void netsim_netdev_send_completed(NetClientState* nc, ssize_t len) {
    VLOG(2) << "NETSIM: async completed";
    NetsimNicState* s = (NetsimNicState*)nc;
    // Clean-up the saved buffer.
    s->netsim->async_tx.reset();

    s->netsim->transport->next_recv();
}

bool netsim_netdev_send(NetClientState* nc, std::unique_ptr<std::vector<uint8_t>> buf) {
    // From netsim to guest.
    VLOG(2) << "NETSIM: send (netsim -> guest)";

    NetsimNicState* s = (NetsimNicState*)nc;
    if (s->netsim->async_tx) {
        LOG(DFATAL) << "Netsim recv: async_tx not empty, dropping packet - this is a bug and "
                       "network performance may be affected";
        return false;
    }

    if (qemu_send_packet_async(nc, buf->data(), buf->size(), netsim_netdev_send_completed) == 0) {
        VLOG(2) << "NETSIM: storing async_tx";
        // Keep the buffer alive until the send completes.
        s->netsim->async_tx = std::move(buf);
        return false;
    } else {
        return true;
    }
}

ssize_t netsim_netdev_receive(NetClientState* nc, const uint8_t* buf, size_t size) {
    // From guest to netsim.
    VLOG(2) << "NETSIM: receive (netsim <- guest)";
    NetsimNicState* s = (NetsimNicState*)nc;

    if (s->is_wifi) {
        // Filter out spurious garbage data from the guest.
        const goldfish::network::GenericNetlinkMessage msg(buf, size);
        if (msg.genericNetlinkHeader()->cmd != HWSIM_CMD_FRAME) {
            VLOG(1) << "Not sending junk frame";
            return size;
        }

        ::netsim::packet::PacketRequest toSend;
        toSend.set_packet(
                std::string_view(reinterpret_cast<const char*>(msg.data()), msg.dataLen()));
        s->netsim->transport->send(std::move(toSend));
    } else {
        ::netsim::packet::PacketRequest toSend;
        toSend.set_packet(std::string_view(reinterpret_cast<const char*>(buf), size));
        s->netsim->transport->send(std::move(toSend));
    }

    // TODO(whollins): Should we try to detect netsimd connection drop and set link down?

    // Return consumed buffer size.
    return size;
}

void netsim_netdev_link_status_changed(NetClientState* nc) {
    VLOG(1) << "NETSIM: link status changed: " << !nc->link_down;
}

void netsim_netdev_cleanup(NetClientState* nc) {
    NetsimNicState* s = (NetsimNicState*)nc;
    bool locked = bql_locked();
    if (locked) {
        bql_unlock();
    }
    delete s->netsim;
    if (locked) {
        bql_lock();
    }
}

NetClientInfo netsim_netdev_nic_info = {
    // We could add our own value in qapi/net.json but
    // it doesn't seem to be necessary.
    // .type = NET_CLIENT_DRIVER_NETSIM,
    .type = NET_CLIENT_DRIVER_NONE,
    .size = sizeof(NetsimNicState),
    .receive = netsim_netdev_receive,
    .cleanup = netsim_netdev_cleanup,
    // TODO consider adding a receive iov handler.
    // ssize_t netsim_netdev_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
    //.receive_iov = netsim_netdev_receive_iov,
    .link_status_changed = netsim_netdev_link_status_changed,
};

void netsim_netdev_realize(DeviceState* dev, Error** errp) {
    add_deletable_object(OBJECT(dev));
    if (dev->id == nullptr) {
        error_setg(errp, "id attribute must be set");
        return;
    }

    VLOG(1) << "Realizing netsim netdev: " << dev->id;

    NetsimNetdev* netsim_netdev = NETSIM_NETDEV(dev);
    if (netsim_netdev->chip_kind == 0) {
        // Unset so default to wifi.
        netsim_netdev->chip_kind = ::netsim::common::ChipKind::WIFI;
    }

    NetClientState* nc;
    nc = qemu_find_netdev(dev->id);
    if (nc != nullptr) {
        error_setg(errp, "netdev with id already exists: %s", dev->id);
        return;
    }

    NetClientState* peer = nullptr;
    nc = qemu_new_net_client(&netsim_netdev_nic_info, peer, "netsim", dev->id);
    nc->is_netdev = true;

    NetsimNicState* s = (NetsimNicState*)nc;

    s->netsim = new NetsimState;
    s->netsim->transport =
            std::make_unique<NetsimTransport>([nc](::netsim::packet::PacketResponse* packet) {
                if (packet->has_packet()) {
                    return netsim_netdev_send(nc, ToUniqueVec(packet->mutable_packet()));
                } else {
                    LOG(WARNING) << "Unexpected packet " << packet->DebugString();
                    // Try to receive next packet immediately.
                    return true;
                }
            });
    s->is_wifi = netsim_netdev->chip_kind == ::netsim::common::ChipKind::WIFI;

    ::netsim::startup::Chip chip;
    chip.set_kind(netsim_netdev->chip_kind);
    if (auto status = s->netsim->transport->initialize(std::move(chip)); !status.ok()) {
        error_setg(errp, "failed to initialize netsim transport %s: %s", dev->id,
                   status.ToString().c_str());
        return;
    }
}

void netsim_netdev_unrealize(DeviceState* dev) {
    VLOG(1) << "Unrealizing netsim netdev: " << dev->id;

    NetClientState* nc;
    nc = qemu_find_netdev(dev->id);
    if (nc == nullptr) {
        LOG(ERROR) << "Can't find netdev to delete: " << dev->id;
        return;
    }
    qemu_del_net_client(nc);
}

void netsim_netdev_set_mode(Object* obj, Visitor* v, const char* name, void* opaque, Error** errp) {
    auto* netdev = NETSIM_NETDEV(obj);
    char* mode;
    if (!visit_type_str(v, name, &mode, errp)) {
        return;
    }
    VLOG(1) << "netsim-netdev: mode = " << mode;
    std::string s_mode(mode);
    if (s_mode == "wifi") {
        netdev->chip_kind = ::netsim::common::ChipKind::WIFI;
    } else if (s_mode == "ethernet") {
        netdev->chip_kind = ::netsim::common::ChipKind::ETHERNET;
    } else if (s_mode == "cellular") {
        netdev->chip_kind = ::netsim::common::ChipKind::CELLULAR_DATA;
    }
}

void netsim_netdev_class_init(ObjectClass* oc, void* data) {
    object_class_property_add(oc, "mode", "str", nullptr, netsim_netdev_set_mode, nullptr, nullptr);

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = netsim_netdev_realize;
    dc->unrealize = netsim_netdev_unrealize;
}

const TypeInfo netsim_netdev_type_info = {
    .name = TYPE_NETSIM_NETDEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(NetsimNetdev),
    .class_init = netsim_netdev_class_init,
};

}  // namespace

void netsim_netdev_register_types(void) {
    type_register_static(&goldfish::netsim::netsim_netdev_type_info);
}

}  // namespace goldfish::netsim
