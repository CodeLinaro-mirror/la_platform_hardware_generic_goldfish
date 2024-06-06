
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/module.h"
// IWYU pragma: end_keep
// clang-format on

#ifdef _WIN32
#define LIB_PREFIX ""
#else
#define LIB_PREFIX "lib"
#endif

const QemuModinfo qemu_modinfo[] = {
    {
        /* hw-display-virtio-gpu.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-gpu",
        .objs = ((const char *[]){"virtio-gpu-base", "virtio-gpu-device",
                                  "vhost-user-gpu", NULL}),
    },
    {
        /* hw-display-virtio-gpu-rutabaga.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-gpu-rutabaga",
        .objs = ((const char *[]){"virtio-gpu-rutabaga-device", NULL}),
        .deps = ((const char *[]){LIB_PREFIX "hw-display-virtio-gpu", NULL}),
    },
    {
        /* hw-display-virtio-gpu-pci.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-gpu-pci",
        .objs = ((const char *[]){"virtio-gpu-pci-base", "virtio-gpu-pci",
                                  "vhost-user-gpu-pci", NULL}),
    },
    {
        /* hw-display-virtio-gpu-pci-rutabaga.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-gpu-pci-rutabaga",
        .objs = ((const char *[]){"virtio-gpu-rutabaga-pci", NULL}),
        .deps =
            ((const char *[]){LIB_PREFIX "hw-display-virtio-gpu-pci", NULL}),
    },
    {
        /* hw-display-virtio-vga.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-vga",
        .objs = ((const char *[]){"virtio-vga-base", "virtio-vga",
                                  "vhost-user-vga", NULL}),
    },
    {
        /* hw-display-virtio-vga-gl.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-vga-gl",
    },
    {
        /* hw-display-virtio-vga-rutabaga.modinfo */
        .name = LIB_PREFIX "hw-display-virtio-vga-rutabaga",
        .objs = ((const char *[]){"virtio-vga-rutabaga", NULL}),
        .deps = ((const char *[]){LIB_PREFIX "hw-display-virtio-vga", NULL}),
    },
    {
        /* audio-pa.modinfo */
        .name = LIB_PREFIX "audio-pa",
        .objs = ((const char *[]){"audio-pa", NULL}),
    },
    {
        /* accel-tcg-x86_64.modinfo */
        .name = LIB_PREFIX "accel-tcg-x86_64",
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
        .name = LIB_PREFIX "sample",
        .opts = ((const char *[]){"device", NULL}),
        .objs = ((const char *[]){"sample", NULL}),
    },
    {
        .name = LIB_PREFIX "grpc",
        .opts = ((const char *[]){"device", NULL}),
        .objs = ((const char *[]){"grpc", NULL}),
    },
    {
        /* end of list */
    }};
