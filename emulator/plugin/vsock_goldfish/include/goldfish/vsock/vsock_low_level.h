// Copyright 2024 The Android Open Source Project
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

struct virtio_vsock_event;
struct virtio_vsock_hdr;
typedef unsigned VirtIOVSockSendResult;

#include <stdbool.h>
#include "qemu/typedefs.h"
#include "stddef.h"
#include "stdint.h"

#define VirtIOVSockSend_NeedNotify_SHIFT 0
#define VirtIOVSockSend_VqFull_SHIFT 1
#define VirtIOVSockSend_SIZE_SHIFT 2
#define VirtIOVSockSend_NeedNotify_BIT (1U << VirtIOVSockSend_NeedNotify_SHIFT)
#define VirtIOVSockSend_VqFull_BIT (1U << VirtIOVSockSend_VqFull_SHIFT)

#define VirtIOVSockSendNeedNotify(R) ((R) & VirtIOVSockSend_NeedNotify_BIT)
#define VirtIOVSockSendIsVqFull(R) ((R) & VirtIOVSockSend_VqFull_BIT)
#define VirtIOVSockSentSize(R) ((R) >> VirtIOVSockSend_SIZE_SHIFT)

#define MakeVirtIOVSockSendResult(VQ_FULL, NEED_NOTIFY_VQ, SIZE) \
    ((!!(NEED_NOTIFY_VQ) << VirtIOVSockSend_NeedNotify_SHIFT) |  \
     (!!(VQ_FULL) << VirtIOVSockSend_VqFull_SHIFT) | ((SIZE) << VirtIOVSockSend_SIZE_SHIFT))

typedef struct GoldfishVirtIOVSockDevAPI_tag {
    void (*haveHostToGuestPackets)(void* dev);

    VirtIOVSockSendResult (*sendPacketHostToGuest)(void* dev, struct virtio_vsock_hdr* hdr,
                                                   const void* data);

    void (*haveHostToGuestEvents)(void* dev);

    VirtIOVSockSendResult (*sendEventHostToGuest)(void* dev,
                                                  const struct virtio_vsock_event* event);
} GoldfishVirtIOVSockDevAPI;

#ifdef __cplusplus
extern "C" {
#endif

extern void* goldfish_virtio_vsock_impl_realize(void* dev,
                                                const GoldfishVirtIOVSockDevAPI* dev_api);
extern void goldfish_virtio_vsock_impl_unrealize(void* impl);

extern void goldfish_virtio_vsock_set_status(void* impl, uint8_t status);
extern void goldfish_virtio_vsock_accept_guest_to_host_control(void* impl,
                                                               const struct virtio_vsock_hdr* hdr);

extern void* goldfish_virtio_vsock_accept_guest_to_host_rw_start(
        void* impl, const struct virtio_vsock_hdr* hdr);
extern int goldfish_virtio_vsock_accept_guest_to_host_rw(void* impl, void* stream, const void* data,
                                                         size_t size);
extern void goldfish_virtio_vsock_accept_guest_to_host_rw_end(void* impl, void* stream,
                                                              int erase_stream);

// return: non-zero - notify the vq, zero - do nothing
extern int goldfish_virtio_vsock_handle_host_to_guest(void* impl);
extern int goldfish_virtio_vsock_handle_event_to_guest(void* impl);

extern int goldfish_virtio_vsock_impl_save(const void* impl, QEMUFile* f);
extern int goldfish_virtio_vsock_impl_load(void* impl, QEMUFile* f);

void vsock_low_level_register_types(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
