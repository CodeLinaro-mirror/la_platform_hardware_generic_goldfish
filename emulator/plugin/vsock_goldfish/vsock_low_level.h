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

#pragma once

struct virtio_vsock_event;
struct virtio_vsock_hdr;
typedef unsigned VirtIOVSockSendResult;

#include "qemu/typedefs.h"

#define VirtIOVSockSend_NeedNotify_SHIFT 0
#define VirtIOVSockSend_VqFull_SHIFT 1
#define VirtIOVSockSend_SIZE_SHIFT 2
#define VirtIOVSockSend_NeedNotify_BIT (1U << VirtIOVSockSend_NeedNotify_SHIFT)
#define VirtIOVSockSend_VqFull_BIT (1U << VirtIOVSockSend_VqFull_SHIFT)

#define VirtIOVSockSendNeedNotify(R) ((R) & VirtIOVSockSend_NeedNotify_BIT)
#define VirtIOVSockSendIsVqFull(R) ((R) & VirtIOVSockSend_VqFull_BIT)
#define VirtIOVSockSentSize(R) ((R) >> VirtIOVSockSend_SIZE_SHIFT)

#define MakeVirtIOVSockSendResult(VQ_FULL, NEED_NOTIFY_VQ, SIZE) ( \
        (!!(NEED_NOTIFY_VQ) << VirtIOVSockSend_NeedNotify_SHIFT) | \
        (!!(VQ_FULL) << VirtIOVSockSend_VqFull_SHIFT) | \
        ((SIZE) << VirtIOVSockSend_SIZE_SHIFT) \
    )

typedef struct GoldfishVirtIOVSockDevAPI_tag {
    void (*haveHostToGuestPackets)(void *dev);

    VirtIOVSockSendResult (*sendPacketHostToGuest)(
        void *dev,
        struct virtio_vsock_hdr* hdr,
        const void* data);

    void (*haveHostToGuestEvents)(void *dev);

    VirtIOVSockSendResult (*sendEventHostToGuest)(
        void *dev,
        const struct virtio_vsock_event *event);
} GoldfishVirtIOVSockDevAPI;

#ifdef __cplusplus
extern "C" {
#endif

extern void* goldfish_virtio_vsock_impl_realize(
    void *dev, const GoldfishVirtIOVSockDevAPI *dev_api);
extern void goldfish_virtio_vsock_impl_unrealize(void *impl);

extern void goldfish_virtio_vsock_set_status(void *impl, uint8_t status);
extern void goldfish_virtio_vsock_accept_guest_to_host(
    void *impl, const struct virtio_vsock_hdr* hdr, const void *data);

// return: non-zero - notify the vq, zero - do nothing
extern int goldfish_virtio_vsock_handle_host_to_guest(void *impl);
extern int goldfish_virtio_vsock_handle_event_to_guest(void *impl);

extern int goldfish_virtio_vsock_impl_save(const void *impl, QEMUFile *f);
extern int goldfish_virtio_vsock_impl_load(void *impl, QEMUFile *f);

#ifdef __cplusplus
}  /* extern "C" */
#endif
