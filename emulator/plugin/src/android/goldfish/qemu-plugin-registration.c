
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/module.h"
// IWYU pragma: end_keep
// clang-format on

const QemuModinfo qemu_modinfo[] = {
    {
        /* hw-display-virtio-gpu-rutabaga.modinfo */
        .name = "hw-display-virtio-gpu-rutabaga",
        .objs = ((const char *[]){"virtio-gpu-rutabaga-device", NULL}),
        .deps = ((const char *[]){"hw-display-virtio-gpu", NULL}),
    },
    {
        /* hw-display-virtio-gpu-pci-rutabaga.modinfo */
        .name = "hw-display-virtio-gpu-pci-rutabaga",
        .objs = ((const char *[]){"virtio-gpu-rutabaga-pci", NULL}),
        .deps = ((const char *[]){"hw-display-virtio-gpu-pci", NULL}),
    },
    {
        /* hw-display-virtio-vga-rutabaga.modinfo */
        .name = "hw-display-virtio-vga-rutabaga",
        .objs = ((const char *[]){"virtio-vga-rutabaga", NULL}),
        .deps = ((const char *[]){"hw-display-virtio-vga", NULL}),
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
