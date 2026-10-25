/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * The VirtIOGPURutabaga declarations of QEMU's include/hw/virtio/virtio-gpu.h,
 * for the plugin's virtio-gpu-gfxstream device (shared by the device, its PCI
 * wrapper and virtio_gpu_glue).  Must be included after qemu/osdep.h and
 * hw/virtio/virtio-gpu.h, which still provide MAX_SLOTS, MemoryRegionInfo and
 * the virtio_gpu_rutabaga_context / _resource list types.
 *
 * TODO(b/576450291): when the in-tree virtio-gpu-rutabaga device is removed
 * from QEMU, MAX_SLOTS, struct MemoryRegionInfo and the
 * virtio_gpu_rutabaga_context / virtio_gpu_rutabaga_resource list types move
 * from hw/virtio/virtio-gpu.h into this header.
 */

#pragma once

#define TYPE_VIRTIO_GPU_GFXSTREAM "virtio-gpu-gfxstream-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOGPUGfxstream, VIRTIO_GPU_GFXSTREAM)

struct gfxstream;
struct virtio_gpu_gfxstream_async_signaled_fence;

struct VirtIOGPUGfxstream {
    VirtIOGPU parent_obj;
    struct VMChangeStateEntry* vm_state_entry;
    struct MemoryRegionInfo memory_regions[MAX_SLOTS];
    uint32_t num_mapped_slots;
    QTAILQ_HEAD(, virtio_gpu_rutabaga_context) contexts;
    /*
     * Fences the renderer has signaled (pushed from its own worker threads)
     * but that the main loop has not yet matched against g->fenceq and
     * delivered to the guest.
     */
    QSLIST_HEAD(, virtio_gpu_gfxstream_async_signaled_fence)
    async_signaled_fenceq;
    QEMUBH* async_signaled_fence_bh;
    uint64_t capset_mask;
    char* wsi;
    char* renderer_features;
    char* snapshot_directory;
    /* GfxstreamVerifyMode; set through the "x-verify-state" enum property. */
    int verify_state_mode;
    /*
     * True from the moment virtio_gpu_gfxstream_load() starts gfxstream_restore()
     * until virtio_gpu_post_load() has finished.  If a reset or another
     * pre_load sees it set, a restore never completed (it failed in this
     * device or load_snapshot() bailed out on a later one) and the gfxstream
     * instance may hold objects QEMU's tables know nothing about: force
     * virtio_gpu_gfxstream_reinit().  Cleared by virtio_gpu_gfxstream_init().
     */
    bool restore_incomplete;
    bool headless;
    uint32_t num_capsets;
    struct gfxstream* gfxstream;
};
