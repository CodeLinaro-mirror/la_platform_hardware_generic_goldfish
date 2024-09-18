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

#include "vsock_low_level.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/qdev-properties.h"
#include "standard-headers/linux/virtio_vsock.h"
#include "migration/vmstate.h"
#include "qom/object.h"
// IWYU pragma: end_keep
// clang-format on

typedef struct VirtIOVSock_tag {
    VirtIODevice parent;
    uint64_t guest_cid;
    void* impl; /* see virtio_vsock_init/virtio_vsock_deinit */

    /* do not save/load below to the snapshot */
    VirtQueue* host_to_guest_vq;
    VirtQueue* guest_to_host_vq;
    VirtQueue* host_to_guest_event_vq;
} VirtIOVSock;

#define TYPE_VIRTIO_VSOCK "virtio-goldfish-vsock"
DECLARE_INSTANCE_CHECKER(VirtIOVSock, VIRTIO_VSOCK, TYPE_VIRTIO_VSOCK);

#define VMADDR_CID_HOST 2

#define DEBUG_MSG(FMT, ...)  // fprintf(stderr, "%s:%d: " FMT "\n", __func__, __LINE__, __VA_ARGS__)

static VirtQueueElement* virtqueue_pop_elem(VirtQueue* const vq) {
    return (VirtQueueElement*)virtqueue_pop(vq, sizeof(VirtQueueElement));
}

static void virtqueue_consume_elem(VirtQueue* const vq, VirtQueueElement* const e,
                                   const unsigned int len) {
    virtqueue_push(vq, e, len);
    g_free(e);
}

static void virtio_vsock_have_host_to_guest_packets(void* const s_raw) {
    VirtIOVSock* const s = (VirtIOVSock*)s_raw;
    virtio_notify(&s->parent, s->host_to_guest_vq);
}

static VirtIOVSockSendResult virtio_vsock_send_packet_host_to_guest(
        void* const s_raw, struct virtio_vsock_hdr* const hdr, const void* const data) {
    bool need_notify = false;
    bool is_vq_full = false;
    size_t bytes_sent = 0;

    VirtIOVSock* const s = (VirtIOVSock*)s_raw;
    VirtQueue* const vq = s->host_to_guest_vq;
    const uint8_t* data8 = (const uint8_t*)data;
    size_t data8_size = hdr->len;

    hdr->src_cid = VMADDR_CID_HOST;
    hdr->dst_cid = s->guest_cid;
    hdr->type = VIRTIO_VSOCK_TYPE_STREAM;
    hdr->flags = (hdr->op == VIRTIO_VSOCK_OP_SHUTDOWN)
                         ? (VIRTIO_VSOCK_SHUTDOWN_RCV | VIRTIO_VSOCK_SHUTDOWN_SEND)
                         : 0;

    do {
        VirtQueueElement* const e = virtqueue_pop_elem(vq);
        if (!e) {
            is_vq_full = true;
            break;
        }

        const size_t e_size = iov_size(e->in_sg, e->in_num);
        if (e_size < sizeof(struct virtio_vsock_hdr)) {
            virtqueue_consume_elem(vq, e, 0);
            need_notify = true;
            continue;
        }

        const size_t data8_chunk_size = MIN(data8_size, e_size - sizeof(struct virtio_vsock_hdr));

        hdr->len = data8_chunk_size;
        iov_from_buf(e->in_sg, e->in_num, 0, hdr, sizeof(*hdr));
        iov_from_buf(e->in_sg, e->in_num, sizeof(*hdr), data8, data8_chunk_size);
        virtqueue_consume_elem(vq, e, sizeof(*hdr) + data8_chunk_size);
        need_notify = true;

        data8_size -= data8_chunk_size;
        bytes_sent += data8_chunk_size;
    } while (data8_size);

    return MakeVirtIOVSockSendResult(is_vq_full, need_notify, bytes_sent);
}

static void virtio_vsock_handle_host_to_guest(VirtIODevice* const dev, VirtQueue* const vq) {
    if (goldfish_virtio_vsock_handle_host_to_guest(VIRTIO_VSOCK(dev)->impl)) {
        virtio_notify(dev, vq);
    }
}

static void virtio_vsock_parse_guest_to_host(VirtIOVSock* const s, void* impl,
                                             const struct iovec* iovec, unsigned int iovec_num) {
    const size_t packet_size = iov_size(iovec, iovec_num);

    struct virtio_vsock_hdr hdr;
    if (packet_size < sizeof(hdr)) {
        return;
    }

    iov_to_buf(iovec, iovec_num, 0, &hdr, sizeof(hdr));
    if (packet_size < (sizeof(hdr) + hdr.len)) {
        return;
    }

    if (hdr.type != VIRTIO_VSOCK_TYPE_STREAM) {
        return;
    }

    if (hdr.dst_cid != VMADDR_CID_HOST) {
        return;
    }

    if (hdr.src_cid != s->guest_cid) {
        return;
    }

    if (hdr.op != VIRTIO_VSOCK_OP_RW) {
        goldfish_virtio_vsock_accept_guest_to_host_control(impl, &hdr);
        return;
    }

    /* handle VIRTIO_VSOCK_OP_RW */
    void* stream = goldfish_virtio_vsock_accept_guest_to_host_rw_start(impl, &hdr);
    if (!stream) {
        return;
    }

    int erase_stream = 0;
    size_t size = hdr.len;
    size_t offset = sizeof(hdr);

    for (; iovec_num && size; --iovec_num, ++iovec) {
        const size_t iov_len = iovec->iov_len;
        const size_t skip_size = MIN(offset, iov_len);
        const size_t data_size = MIN(size, iov_len - skip_size);

        if (data_size > 0) {
            void* const data = iovec->iov_base + skip_size;
            erase_stream =
                    goldfish_virtio_vsock_accept_guest_to_host_rw(impl, stream, data, data_size);
            if (erase_stream) {
                break;
            }
            size -= data_size;
        }
        offset -= skip_size;
    }

    goldfish_virtio_vsock_accept_guest_to_host_rw_end(impl, stream, erase_stream);
}

static void virtio_vsock_handle_guest_to_host(VirtIODevice* const dev, VirtQueue* const vq) {
    VirtIOVSock* const s = VIRTIO_VSOCK(dev);
    void* const impl = s->impl;
    bool consumed = false;

    while (true) {
        VirtQueueElement* const e = virtqueue_pop_elem(vq);
        if (e) {
            virtio_vsock_parse_guest_to_host(s, impl, e->out_sg, e->out_num);
            virtqueue_consume_elem(vq, e, 0);
            consumed = true;
        } else {
            break;
        }
    }

    if (consumed) {
        virtio_notify(dev, vq);
    }

    virtio_vsock_handle_host_to_guest(dev, s->host_to_guest_vq);
}

static void virtio_vsock_have_host_to_guest_events(void* const s_raw) {
    VirtIOVSock* const s = (VirtIOVSock*)s_raw;
    virtio_notify(&s->parent, s->host_to_guest_event_vq);
}

static VirtIOVSockSendResult virtio_vsock_send_event_host_to_guest(
        void* const s_raw, const struct virtio_vsock_event* const event) {
    bool need_notify = false;
    bool is_vq_full = false;

    VirtIOVSock* const s = (VirtIOVSock*)s_raw;
    VirtQueue* const vq = s->host_to_guest_event_vq;

    while (true) {
        VirtQueueElement* const e = virtqueue_pop_elem(vq);
        if (!e) {
            is_vq_full = true;
            break;
        }

        const size_t e_size = iov_size(e->in_sg, e->in_num);
        if (e_size < sizeof(struct virtio_vsock_event)) {
            virtqueue_consume_elem(vq, e, 0);
            need_notify = true;
            continue;
        }

        iov_from_buf(e->in_sg, e->in_num, 0, event, sizeof(*event));
        virtqueue_consume_elem(vq, e, sizeof(*event));
        need_notify = true;
        break;
    }

    return MakeVirtIOVSockSendResult(is_vq_full, need_notify, 1U);
}

static void virtio_vsock_handle_event_to_guest(VirtIODevice* const dev, VirtQueue* const vq) {
    if (goldfish_virtio_vsock_handle_event_to_guest(VIRTIO_VSOCK(dev)->impl)) {
        virtio_notify(dev, vq);
    }
}

#define VIRTIO_VSOCK_QUEUE_SIZE 128
#define VIRTIO_VSOCK_EVENT_QUEUE_SIZE 16

// virtio spec 5.10.2
enum virtio_vsock_queue_id {
    VIRTIO_VSOCK_QUEUE_ID_RX = 0,    // host to guest
    VIRTIO_VSOCK_QUEUE_ID_TX = 1,    // guest to host
    VIRTIO_VSOCK_QUEUE_ID_EVENT = 2  // host to guest
};

static void virtio_vsock_device_realize(DeviceState* const dev, Error** errp) {
    static const GoldfishVirtIOVSockDevAPI dev_api = {
            .haveHostToGuestPackets = &virtio_vsock_have_host_to_guest_packets,
            .sendPacketHostToGuest = &virtio_vsock_send_packet_host_to_guest,
            .haveHostToGuestEvents = &virtio_vsock_have_host_to_guest_events,
            .sendEventHostToGuest = &virtio_vsock_send_event_host_to_guest,
    };

    VirtIOVSock* const s = VIRTIO_VSOCK(dev);
    DEBUG_MSG("s=%p", s);

    if ((s->guest_cid <= VMADDR_CID_HOST) || (s->guest_cid == UINT32_MAX) ||
        (s->guest_cid == UINT64_MAX)) {
        error_setg(errp, "The '%s' property value (%" PRIu64 ") is incorrect", "guest-cid",
                   s->guest_cid);
        return;
    }

    VirtIODevice* const v = &s->parent;
    virtio_init(v, VIRTIO_ID_VSOCK, sizeof(struct virtio_vsock_config));
    s->host_to_guest_vq =
            virtio_add_queue(v, VIRTIO_VSOCK_QUEUE_SIZE, &virtio_vsock_handle_host_to_guest);
    s->guest_to_host_vq =
            virtio_add_queue(v, VIRTIO_VSOCK_QUEUE_SIZE, &virtio_vsock_handle_guest_to_host);
    s->host_to_guest_event_vq =
            virtio_add_queue(v, VIRTIO_VSOCK_EVENT_QUEUE_SIZE, &virtio_vsock_handle_event_to_guest);

    s->impl = goldfish_virtio_vsock_impl_realize(s, &dev_api);
    if (!s->impl) {
        virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_EVENT);
        virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_TX);
        virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_RX);
        virtio_cleanup(v);
        error_setg(errp, "virtio_vsock_impl_init failed");
    }
}

static void virtio_vsock_device_unrealize(DeviceState* const dev) {
    VirtIOVSock* s = VIRTIO_VSOCK(dev);
    DEBUG_MSG("s=%p", s);

    VirtIODevice* v = &s->parent;
    goldfish_virtio_vsock_impl_unrealize(s->impl);
    virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_EVENT);
    virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_TX);
    virtio_del_queue(v, VIRTIO_VSOCK_QUEUE_ID_RX);
    virtio_cleanup(v);
}

static uint64_t virtio_vsock_device_get_features(VirtIODevice* const dev,
                                                 const uint64_t requested_features, Error** errp) {
    DEBUG_MSG("dev=%p requested_features=0x%lx", dev, (long)requested_features);
    return requested_features;
}

/**********************************************************************/
static const Property virtio_vsock_properties[] = {
        DEFINE_PROP_UINT64("guest-cid", VirtIOVSock, guest_cid, 0),
        DEFINE_PROP_END_OF_LIST(),
};

static void virtio_vsock_set_status(VirtIODevice* const dev, const uint8_t status) {
    DEBUG_MSG("dev=%p status=0x%02X", dev, status);
    goldfish_virtio_vsock_set_status(VIRTIO_VSOCK(dev)->impl, status);
}

static void virtio_vsock_get_config(VirtIODevice* const dev, uint8_t* const raw) {
    DEBUG_MSG("dev=%p raw=%p", dev, raw);

    struct virtio_vsock_config cfg = {.guest_cid = VIRTIO_VSOCK(dev)->guest_cid};
    memcpy(raw, &cfg, sizeof(cfg));
}

static void virtio_vsock_set_config(VirtIODevice* const dev, const uint8_t* const raw) {
    DEBUG_MSG("dev=%p raw=%p", dev, raw);

    struct virtio_vsock_config cfg;
    memcpy(&cfg, raw, sizeof(cfg));
    VIRTIO_VSOCK(dev)->guest_cid = cfg.guest_cid;
}

static int vmstate_info_virtio_vsock_impl_load(QEMUFile* const f, void* const opaque, size_t size,
                                               const VMStateField* field) {
    return goldfish_virtio_vsock_impl_load(opaque, f);
}

static int vmstate_info_virtio_vsock_impl_save(QEMUFile* const f, void* const opaque, size_t size,
                                               const VMStateField* field, JSONWriter* json) {
    return goldfish_virtio_vsock_impl_save(opaque, f);
}

static const VMStateInfo vmstate_info_virtio_vsock_impl = {
        .name = "vmstate_info_virtio_vsock_impl",
        .get = &vmstate_info_virtio_vsock_impl_load,
        .put = &vmstate_info_virtio_vsock_impl_save,
};

static const VMStateDescription vmstate_virtio_vsock = {
        .name = TYPE_VIRTIO_VSOCK,
        .minimum_version_id = 0,
        .version_id = 0,
        .fields = (VMStateField[]){VMSTATE_VIRTIO_DEVICE, VMSTATE_UINT64(guest_cid, VirtIOVSock),
                                   VMSTATE_POINTER(impl, VirtIOVSock, 0,
                                                   vmstate_info_virtio_vsock_impl, void*),
                                   VMSTATE_END_OF_LIST()},
};

static void virtio_vsock_class_init(ObjectClass* klass, void* data) {
    DEBUG_MSG("klass=%p data=%p", klass, data);

    DeviceClass* dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, (Property*)virtio_vsock_properties);
    dc->vmsd = &vmstate_virtio_vsock;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    VirtioDeviceClass* vdc = VIRTIO_DEVICE_CLASS(klass);
    vdc->realize = &virtio_vsock_device_realize;
    vdc->unrealize = &virtio_vsock_device_unrealize;
    vdc->get_features = &virtio_vsock_device_get_features;
    vdc->get_config = &virtio_vsock_get_config;
    vdc->set_config = &virtio_vsock_set_config;
    vdc->set_status = &virtio_vsock_set_status;
}

static const TypeInfo virtio_vsock_typeinfo = {
        .name = TYPE_VIRTIO_VSOCK,
        .parent = TYPE_VIRTIO_DEVICE,
        .instance_size = sizeof(VirtIOVSock),
        .class_init = &virtio_vsock_class_init,
};

/******************** VirtioVsockPCI *****************/
typedef struct VirtioVsockPCITag {
    VirtIOPCIProxy parent;
    VirtIOVSock vdev;
} VirtioVsockPCI;

#define TYPE_VIRTIO_VSOCK_PCI_GENERIC TYPE_VIRTIO_VSOCK "-pci"
#define TYPE_VIRTIO_VSOCK_PCI_BASE TYPE_VIRTIO_VSOCK_PCI_GENERIC "-base"
DECLARE_INSTANCE_CHECKER(VirtioVsockPCI, VIRTIO_VSOCK_PCI, TYPE_VIRTIO_VSOCK_PCI_GENERIC);

static Property virtio_vsock_pci_properties[] = {
        DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
        DEFINE_PROP_END_OF_LIST(),
};

static void virtio_vsock_pci_realize(VirtIOPCIProxy* vpci_dev, Error** errp) {
    VirtioVsockPCI* dev = VIRTIO_VSOCK_PCI(vpci_dev);
    DeviceState* vdev = DEVICE(&dev->vdev);

    DEBUG_MSG("vpci_dev=%p vdev=%p", vpci_dev, vdev);
    virtio_pci_force_virtio_1(vpci_dev);
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_vsock_pci_class_init(ObjectClass* klass, void* data) {
    DEBUG_MSG("klass=%p data=%p", klass, data);

    DeviceClass* dc = DEVICE_CLASS(klass);
    VirtioPCIClass* k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass* pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_vsock_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, virtio_vsock_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VSOCK;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void virtio_vsock_pci_instance_init(Object* obj) {
    VirtioVsockPCI* dev = VIRTIO_VSOCK_PCI(obj);
    DEBUG_MSG("dev=%p", dev);
    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_VSOCK);
}

static const VirtioPCIDeviceTypeInfo virtio_vsock_pci_typeinfo = {
        .base_name = TYPE_VIRTIO_VSOCK_PCI_BASE,
        .generic_name = TYPE_VIRTIO_VSOCK_PCI_GENERIC,
        .non_transitional_name = TYPE_VIRTIO_VSOCK_PCI_GENERIC "-non-transitional",
        .instance_size = sizeof(VirtioVsockPCI),
        .instance_init = virtio_vsock_pci_instance_init,
        .class_init = virtio_vsock_pci_class_init,
};

static void register_types(void) {
    DEBUG_MSG("registering %s and %s", TYPE_VIRTIO_VSOCK, TYPE_VIRTIO_VSOCK_PCI_GENERIC);
    type_register_static(&virtio_vsock_typeinfo);
    virtio_pci_types_register(&virtio_vsock_pci_typeinfo);
}

type_init(register_types);
