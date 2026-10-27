/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * PCI transport for virtio-gpu-gfxstream: QEMU's
 * hw/display/virtio-gpu-pci-rutabaga.c with the type renamed and the type
 * registration done from virtio_gpu_gfxstream_register_types().
 */

#include "goldfish/devices/virtio_gpu_gfxstream/virtio_gpu_gfxstream.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/core/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-gpu-pci.h"
#include "qom/object.h"
// IWYU pragma: end_keep
// clang-format on

#include "virtio_gpu_gfxstream_internal.h"

#define TYPE_VIRTIO_GPU_GFXSTREAM_PCI "virtio-gpu-gfxstream-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOGPUGfxstreamPCI, VIRTIO_GPU_GFXSTREAM_PCI)

struct VirtIOGPUGfxstreamPCI {
    VirtIOGPUPCIBase parent_obj;

    VirtIOGPUGfxstream vdev;
};

static void virtio_gpu_gfxstream_initfn(Object* obj) {
    VirtIOGPUGfxstreamPCI* dev = VIRTIO_GPU_GFXSTREAM_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_GPU_GFXSTREAM);
    VIRTIO_GPU_PCI_BASE(obj)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}

static const TypeInfo virtio_gpu_gfxstream_pci_info[] = {
    {.name = TYPE_VIRTIO_GPU_GFXSTREAM_PCI,
     .parent = TYPE_VIRTIO_GPU_PCI_BASE,
     .instance_size = sizeof(VirtIOGPUGfxstreamPCI),
     .instance_init = virtio_gpu_gfxstream_initfn,
     .interfaces =
             (const InterfaceInfo[]){
                 {INTERFACE_CONVENTIONAL_PCI_DEVICE},
                 {},
             }},
};

void virtio_gpu_gfxstream_pci_register_types(void) {
    type_register_static_array(virtio_gpu_gfxstream_pci_info,
                               ARRAY_SIZE(virtio_gpu_gfxstream_pci_info));
}
