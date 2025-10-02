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

#include "goldfish/net/netsim-netdev.h"

#include <memory>

#include "absl/log/log.h"

extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
}

#undef send

#include "NetsimTransport.h"

namespace goldfish::net {

struct NetsimNetdev {
    DeviceClass parent_class;
    char* grpc_endpoint;
};

#define TYPE_NETSIM_NETDEV "netsim-netdev"
#define NETSIM_NETDEV(obj) OBJECT_CHECK(NetsimNetdev, (obj), TYPE_NETSIM_NETDEV)
#define NETSIM_NETDEV_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(NetsimNetdev, obj, TYPE_NETSIM_NETDEV)

namespace {

struct NetsimState {
    std::unique_ptr<NetsimTransport> transport;
    std::unique_ptr<std::vector<uint8_t>> async_tx;
};

struct NetsimNicState {
    // Note C struct so no c/d-tor.
    NetClientState nc;
    NetsimState* netsim;
};

void netsim_send_completed(NetClientState* nc, ssize_t len) {
    VLOG(2) << "NETSIM: async completed";
    NetsimNicState* s = (NetsimNicState*)nc;
    // Clean-up the saved buffer.
    s->netsim->async_tx.reset();

    s->netsim->transport->next_recv();
}

void netsim_send(NetClientState* nc, std::unique_ptr<std::vector<uint8_t>> buf) {
    // From netsim to guest.
    VLOG(2) << "NETSIM: send (netsim -> guest)";

    NetsimNicState* s = (NetsimNicState*)nc;
    if (s->netsim->async_tx) {
        LOG(DFATAL) << "Netsim recv: async_tx not empty, dropping packet - this is a bug and "
                       "network performance may be affected";
        return;
    }

    if (qemu_send_packet_async(nc, buf->data(), buf->size(), netsim_send_completed) == 0) {
        VLOG(2) << "NETSIM: storing async_tx";
        // Keep the buffer alive until the send completes.
        s->netsim->async_tx = std::move(buf);
    } else {
        s->netsim->transport->next_recv();
    }
}

ssize_t netsim_receive(NetClientState* nc, const uint8_t* buf, size_t size) {
    // From guest to netsim.
    VLOG(2) << "NETSIM: receive (netsim <- guest)";
    NetsimNicState* s = (NetsimNicState*)nc;
    // Return value ignored.
    s->netsim->transport->send(buf, size);

    // TODO(whollins): Should we try to detect netsimd connection drop and set link down?

    // Return consumed buffer size.
    return size;
}

void netsim_link_status_changed(NetClientState* nc) {
    VLOG(1) << "NETSIM: link status changed: " << !nc->link_down;
}

void netsim_cleanup(NetClientState* nc) {
    NetsimNicState* s = (NetsimNicState*)nc;
    // This calls NetsimTransport's destructor, which calls cancel and await
    delete s->netsim;
}

NetClientInfo net_netsim_info = {
    // We could add our own value in qapi/net.json but
    // it doesn't seem to be necessary.
    // .type = NET_CLIENT_DRIVER_NETSIM,
    .type = NET_CLIENT_DRIVER_NONE,
    .size = sizeof(NetsimNicState),
    .receive = netsim_receive,
    .cleanup = netsim_cleanup,
    // TODO consider adding a receive iov handler.
    // ssize_t netsim_receive_iov(NetClientState *nc, const struct iovec *iov, int iovcnt)
    //.receive_iov = netsim_receive_iov,
    .link_status_changed = netsim_link_status_changed,
};

void netsim_netdev_realize(DeviceState* dev, Error** errp) {
    if (dev->id == nullptr) {
        error_setg(errp, "id attribute must be set");
        return;
    }

    VLOG(1) << "Realizing netsim netdev: " << dev->id;

    NetsimNetdev* netsim_netdev = NETSIM_NETDEV(dev);
    if (!netsim_netdev->grpc_endpoint) {
        error_setg(errp, "grpc_endpoint attribute is not set");
        return;
    }

    NetClientState* nc;
    nc = qemu_find_netdev(dev->id);
    if (nc != nullptr) {
        error_setg(errp, "netdev with id already exists: %s", dev->id);
        return;
    }

    NetClientState* peer = nullptr;
    nc = qemu_new_net_client(&net_netsim_info, peer, "netsim", dev->id);
    nc->is_netdev = true;

    NetsimNicState* s = (NetsimNicState*)nc;

    s->netsim = new NetsimState;
    s->netsim->transport = std::make_unique<NetsimTransport>(
            netsim_netdev->grpc_endpoint,
            [nc](std::unique_ptr<std::vector<uint8_t>> buf) { netsim_send(nc, std::move(buf)); });

    if (auto status = s->netsim->transport->initialize(); !status.ok()) {
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

    NetsimNetdev* netsim_netdev = NETSIM_NETDEV(dev);
    g_free(netsim_netdev->grpc_endpoint);
}

void netsim_netdev_set_grpc_endpoint(Object* obj, Visitor* v, const char* name, void* opaque,
                                     Error** errp) {
    NetsimNetdev* netsim_netdev = NETSIM_NETDEV(obj);

    char* grpc_endpoint;
    if (!visit_type_str(v, name, &grpc_endpoint, errp)) {
        error_setg(errp, "failed to parse grpc_endpoint string");
        return;
    }

    VLOG(1) << "grpc_endpoint = " << grpc_endpoint;

    netsim_netdev->grpc_endpoint = grpc_endpoint;
}

void netsim_netdev_class_init(ObjectClass* oc, void* data) {
    // TODO(whollins): Consider taking the discovery file directly.
    object_class_property_add(oc, "grpc_endpoint", "str", nullptr, netsim_netdev_set_grpc_endpoint,
                              nullptr, nullptr);

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
    type_register_static(&goldfish::net::netsim_netdev_type_info);
}

}  // namespace goldfish::net
