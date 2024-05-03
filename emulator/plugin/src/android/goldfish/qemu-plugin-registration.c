#include "qemu/osdep.h"
#include "qemu/module.h"
const QemuModinfo qemu_modinfo[] = {
    {
        /* hw-display-virtio-gpu.modinfo */
        .name = "hw-display-virtio-gpu",
        .objs = ((const char *[]){"virtio-gpu-base", "virtio-gpu-device",
                                  "vhost-user-gpu", NULL}),
    },
    {
        /* hw-display-virtio-gpu-pci.modinfo */
        .name = "hw-display-virtio-gpu-pci",
        .objs = ((const char *[]){"virtio-gpu-pci-base", "virtio-gpu-pci",
                                  "vhost-user-gpu-pci", NULL}),
    },
    {
        /* hw-display-virtio-vga.modinfo */
        .name = "hw-display-virtio-vga",
        .objs = ((const char *[]){"virtio-vga-base", "virtio-vga",
                                  "vhost-user-vga", NULL}),
    },
    {
        /* hw-display-virtio-vga-gl.modinfo */
        .name = "hw-display-virtio-vga-gl",
    },
    {
        /* audio-pa.modinfo */
        .name = "audio-pa",
    },
    {
        /* accel-tcg-x86_64.modinfo */
        .name = "accel-tcg-x86_64",
        .arch = "x86_64",
        .objs = ((const char *[]){("tcg"
                                   "-"
                                   "accel"
                                   "-ops"),
                                  NULL}),
    },
    // This will make the module "sample" available inside qemu
    // In our case the sample driver is just very simple and doesn't really
    // do anything.
    // Note that every function you use from qemu must be explicitly exported
    // in windows //external/qemu:platform/windows-amd64/qemu-system-x86_64.def
    {
        .name = "sample",
        .opts = ((const char *[]){"device", NULL}),
    },
    {
        /* end of list */
    }};
