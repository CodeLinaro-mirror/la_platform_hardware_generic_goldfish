// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

/**
 * @file
 * @brief QEMU QOM `SysBusDevice` shim for the legacy Goldfish Pipe v2 device (`GFSH0003`).
 */

#include "goldfish/legacy/goldfish_pipe_low_level.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/atomic.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "hw/core/irq.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "migration/qemu-file-types.h"
#include "migration/snapshot.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "ui/console.h"
#include "google/drivers/hw/acpi/goldfish_defs.h"
#include "google/system/aemu_func_defs.h"
// IWYU pragma: end_keep
// clang-format on

#include "goldfish/legacy/common/c/device_env.h"
#include "goldfish/legacy/common/c/mmio_core.h"
#include "goldfish/legacy/goldfish_gpu/goldfish_gpu_c_api.h"
#include "goldfish_pipe.h"

/** @brief QOM type name for the Goldfish Pipe SysBus device. */
#define TYPE_GOLDFISH_PIPE "goldfish_pipe"
/** @brief Type-checked QOM cast macro to `struct GoldfishPipeState`. */
#define GOLDFISH_PIPE(obj) OBJECT_CHECK(struct GoldfishPipeState, (obj), TYPE_GOLDFISH_PIPE)
/** @brief QOM device ID adopted when `goldfish_pipe` owns the legacy graphics console. */
#define GOLDFISH_LEGACY_GPU_ID "gpu0"
/** @brief Maximum number of QEMU display consoles (`0` = primary/inner, `1` = secondary/outer). */
#define GOLDFISH_PIPE_MAX_DISPLAYS 2U

/**
 * @brief Per-console surface and BGRA frame staging state.
 */
struct GoldfishPipeDisplaySlot {
    QemuConsole* con;      /**< Display console (`head=0` primary, `head=1` secondary foldable). */
    uint8_t* frame_bgra;   /**< Staging buffer for the most recent BGRA frame. */
    size_t frame_bgra_cap; /**< Allocated byte capacity of `frame_bgra`. */
    uint32_t frame_w;      /**< Width of the staged frame in pixels. */
    uint32_t frame_h;      /**< Height of the staged frame in pixels. */
    bool frame_dirty;      /**< True when a new frame is waiting for `present_bh`. */
};

/**
 * @brief QOM instance state for the `"goldfish_pipe"` `SysBusDevice`.
 */
struct GoldfishPipeState {
    SysBusDevice parent_obj; /**< Base QEMU `SysBusDevice` instance. */

    MemoryRegion iomem; /**< `0x2000`-byte little-endian MMIO register region (`0xff001000`). */
    qemu_irq irq;       /**< SysBus interrupt line (x86 GSI 18 / ARM64 SPI 20). */
    QEMUBH* irq_bh;     /**< Bottom half for delivering cross-thread IRQ updates under the BQL. */
    QEMUBH* present_bh; /**< Bottom half for copying posted BGRA frames to `QemuConsole`. */
    QEMUTimer* flush_timer;       /**< Periodic timer flushing async gfxstream pixel readback. */
    struct GoldfishMmioCore core; /**< Function-pointer table and instance for C++ pipe core. */
    int irq_level;                /**< Latest interrupt level (0 or 1), accessed atomically. */

    uint32_t xres;            /**< Configured primary display width (`0` disables graphics). */
    uint32_t yres;            /**< Configured primary display height (`0` disables graphics). */
    uint32_t secondary_xres;  /**< Optional secondary foldable display width (`0` disables). */
    uint32_t secondary_yres;  /**< Optional secondary foldable display height (`0` disables). */
    uint32_t guest_api_level; /**< Android API level of the guest image (required for graphics). */
    char* renderer_features;  /**< Semicolon-separated gfxstream feature overrides. */
    char* snapshot_directory; /**< Base directory for per-snapshot renderer streams. */

    QemuMutex frame_mu; /**< Protects `displays[i]` staging buffers and dirty flags. */
    struct GoldfishPipeDisplaySlot displays[GOLDFISH_PIPE_MAX_DISPLAYS];
    bool frame_mu_inited;      /**< True once `frame_mu` has been initialized. */
    bool graphics_active;      /**< True while `GoldfishGpuCoreStart` is active. */
    bool deletable_registered; /**< True once registered with `add_deletable_object`. */
};

static struct GoldfishPipeState* g_active_graphics_pipe = NULL;

static bool goldfish_pipe_guest_mem_read(void* opaque, uint64_t gpa, void* dst, size_t size) {
    (void)opaque;
    if (size == 0) {
        return true;
    }
    if (dst == NULL || gpa + size < gpa) {
        return false;
    }

    AddressSpace* as = get_qemu_system_address_space();
    assert(as != NULL);

    uint8_t* out = (uint8_t*)dst;
    size_t offset = 0;

    while (offset < size) {
        hwaddr plen = (hwaddr)(size - offset);
        void* ptr =
                address_space_map(as, (hwaddr)(gpa + offset), &plen, false, MEMTXATTRS_UNSPECIFIED);
        if (ptr == NULL || plen == 0) {
            if (ptr != NULL) {
                address_space_unmap(as, ptr, plen, false, 0);
            }
            return false;
        }
        size_t chunk = (plen < (hwaddr)(size - offset)) ? (size_t)plen : (size - offset);
        memcpy(out + offset, ptr, chunk);
        address_space_unmap(as, ptr, plen, false, (hwaddr)chunk);
        offset += chunk;
    }

    return true;
}

static bool goldfish_pipe_guest_mem_write(void* opaque, uint64_t gpa, const void* src,
                                          size_t size) {
    (void)opaque;
    if (size == 0) {
        return true;
    }
    if (src == NULL || gpa + size < gpa) {
        return false;
    }

    AddressSpace* as = get_qemu_system_address_space();
    assert(as != NULL);

    const uint8_t* bytes = (const uint8_t*)src;
    size_t offset = 0;

    while (offset < size) {
        hwaddr plen = (hwaddr)(size - offset);
        void* ptr =
                address_space_map(as, (hwaddr)(gpa + offset), &plen, true, MEMTXATTRS_UNSPECIFIED);
        if (ptr == NULL || plen == 0) {
            if (ptr != NULL) {
                address_space_unmap(as, ptr, plen, true, 0);
            }
            return false;
        }
        size_t chunk = (plen < (hwaddr)(size - offset)) ? (size_t)plen : (size - offset);
        memcpy(ptr, bytes + offset, chunk);
        address_space_unmap(as, ptr, plen, true, (hwaddr)chunk);
        offset += chunk;
    }

    return true;
}

static void goldfish_pipe_irq_bh(void* opaque) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    qemu_set_irq(s->irq, qatomic_read(&s->irq_level));
}

static void goldfish_pipe_irq_set_level(void* opaque, bool level) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    const int target_level = level ? 1 : 0;
    qatomic_set(&s->irq_level, target_level);
    if (bql_locked()) {
        qemu_set_irq(s->irq, target_level);
    } else {
        qemu_bh_schedule(s->irq_bh);
    }
}

static uint64_t goldfish_pipe_mmio_read(void* opaque, hwaddr offset, unsigned size) {
    (void)size;
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    assert(s->core.read != NULL);
    return s->core.read(s->core.core, (uint32_t)offset);
}

static void goldfish_pipe_mmio_write(void* opaque, hwaddr offset, uint64_t val, unsigned size) {
    (void)size;
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    assert(s->core.write != NULL);
    s->core.write(s->core.core, (uint32_t)offset, (uint32_t)val);
}

static const MemoryRegionOps goldfish_pipe_iomem_ops = {
    .read = goldfish_pipe_mmio_read,
    .write = goldfish_pipe_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void goldfish_pipe_gfx_update(void* opaque) {
    (void)opaque;
}

static const GraphicHwOps goldfish_pipe_gfx_ops = {
    .gfx_update = goldfish_pipe_gfx_update,
};

static void goldfish_pipe_present_bh(void* opaque) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    if (!qatomic_read(&s->graphics_active) || s->displays[0].con == NULL) {
        return;
    }

    bool presented_any = false;
    for (size_t i = 0; i < GOLDFISH_PIPE_MAX_DISPLAYS; ++i) {
        struct GoldfishPipeDisplaySlot* slot = &s->displays[i];
        if (slot->con == NULL) {
            continue;
        }

        qemu_mutex_lock(&s->frame_mu);
        if (!slot->frame_dirty || slot->frame_bgra == NULL || slot->frame_w == 0 ||
            slot->frame_h == 0) {
            qemu_mutex_unlock(&s->frame_mu);
            continue;
        }

        DisplaySurface* ds = qemu_console_surface(slot->con);
        const bool replace_surface = (ds == NULL || surface_is_placeholder(ds) ||
                                      surface_width(ds) != (int)slot->frame_w ||
                                      surface_height(ds) != (int)slot->frame_h);
        if (replace_surface) {
            ds = qemu_create_displaysurface((int)slot->frame_w, (int)slot->frame_h);
        }

        uint8_t* dst = (uint8_t*)surface_data(ds);
        const int dst_stride = surface_stride(ds);
        const size_t row_bytes = (size_t)slot->frame_w * 4U;
        if (dst != NULL && dst_stride >= (int)row_bytes) {
            for (uint32_t y = 0; y < slot->frame_h; ++y) {
                memcpy(dst + (size_t)y * (size_t)dst_stride,
                       slot->frame_bgra + (size_t)y * row_bytes, row_bytes);
            }
        }
        slot->frame_dirty = false;
        qemu_mutex_unlock(&s->frame_mu);

        if (replace_surface) {
            dpy_gfx_replace_surface(slot->con, ds);
        }
        dpy_gfx_update_full(slot->con);
        presented_any = true;
    }

    if (presented_any && s->flush_timer != NULL) {
        timer_mod(s->flush_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 42);
    }
}

static void goldfish_pipe_flush_timer_cb(void* opaque) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    if (!qatomic_read(&s->graphics_active)) {
        return;
    }
    // Release the BQL before entering FrameBuffer::flushReadPipeline (m_lock):
    // gfxstream render threads acquire m_lock -> BQL when unmapping host-visible
    // Vulkan ColorBuffers through GoldfishAddressSpaceDevice.
    const bool unlock_bql = bql_locked();
    if (unlock_bql) {
        bql_unlock();
    }
    GoldfishGpuCoreFlush();
    if (unlock_bql) {
        bql_lock();
    }
}

static void goldfish_gpu_on_frame_posted(void* opaque, uint32_t head, uint32_t width,
                                         uint32_t height, const uint8_t* bgra_pixels) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    // The GPU caps both edges at GOLDFISH_GPU_MAX_FRAME_DIM before calling; re-check here so this
    // sink stays safe on its own (the product below must not wrap and the display surface takes
    // `int` dimensions).
    if (!qatomic_read(&s->graphics_active) || head >= GOLDFISH_PIPE_MAX_DISPLAYS || width == 0 ||
        height == 0 || bgra_pixels == NULL || width > GOLDFISH_GPU_MAX_FRAME_DIM ||
        height > GOLDFISH_GPU_MAX_FRAME_DIM) {
        return;
    }

    struct GoldfishPipeDisplaySlot* slot = &s->displays[head];
    if (slot->con == NULL) {
        return;
    }

    const size_t bytes = (size_t)width * (size_t)height * 4U;
    qemu_mutex_lock(&s->frame_mu);
    if (qatomic_read(&s->graphics_active)) {
        if (bytes > slot->frame_bgra_cap) {
            slot->frame_bgra = (uint8_t*)g_realloc(slot->frame_bgra, bytes);
            slot->frame_bgra_cap = bytes;
        }
        memcpy(slot->frame_bgra, bgra_pixels, bytes);
        slot->frame_w = width;
        slot->frame_h = height;
        slot->frame_dirty = true;
        if (s->present_bh != NULL) {
            qemu_bh_schedule(s->present_bh);
        }
    }
    qemu_mutex_unlock(&s->frame_mu);
}

/*
 * `for_process_exit` is set from unrealize (QEMU's exit-time deletable_cleanup(), which runs
 * before the plugin's GF_SHUTDOWN_FUNC): the renderer is quiesced but not destroyed, because
 * FrameBuffer::finalize() tearing down the readback worker's EGL contexts while QEMU exits asserts
 * inside gfxstream (EglContext::~EglContext) and only delays the exit with a crash report. Only a
 * failed realize destroys the renderer.
 */
static void goldfish_pipe_stop_graphics(struct GoldfishPipeState* s, bool for_process_exit) {
    if (s == NULL) {
        return;
    }
    if (g_active_graphics_pipe == s) {
        g_active_graphics_pipe = NULL;
    }
    // Drops the GPU's VMState sections and its snapshot-progress handler (takes the BQL itself).
    goldfish_gpu_vmstate_unregister();
    if (qatomic_read(&s->graphics_active)) {
        qatomic_set(&s->graphics_active, false);
        if (s->flush_timer != NULL) {
            timer_del(s->flush_timer);
        }
        if (s->present_bh != NULL) {
            qemu_bh_cancel(s->present_bh);
        }
        const bool unlock_bql = bql_locked();
        if (unlock_bql) {
            bql_unlock();
        }
        if (for_process_exit) {
            GoldfishGpuCoreStopForProcessExit();
        } else {
            GoldfishGpuCoreStop();
        }
        if (unlock_bql) {
            bql_lock();
        }
    }
    if (s->flush_timer != NULL) {
        timer_free(s->flush_timer);
        s->flush_timer = NULL;
    }
    if (s->present_bh != NULL) {
        qemu_bh_delete(s->present_bh);
        s->present_bh = NULL;
    }
    if (s->frame_mu_inited) {
        qemu_mutex_lock(&s->frame_mu);
        for (size_t i = 0; i < GOLDFISH_PIPE_MAX_DISPLAYS; ++i) {
            g_free(s->displays[i].frame_bgra);
            s->displays[i].frame_bgra = NULL;
            s->displays[i].frame_bgra_cap = 0;
            s->displays[i].frame_w = 0;
            s->displays[i].frame_h = 0;
            s->displays[i].frame_dirty = false;
        }
        qemu_mutex_unlock(&s->frame_mu);
        qemu_mutex_destroy(&s->frame_mu);
        s->frame_mu_inited = false;
    }
}

static void goldfish_pipe_realize(DeviceState* dev, Error** errp) {
    SysBusDevice* sbdev = SYS_BUS_DEVICE(dev);
    struct GoldfishPipeState* s = GOLDFISH_PIPE(dev);

    if ((s->secondary_xres == 0) != (s->secondary_yres == 0)) {
        error_setg(errp,
                   "goldfish_pipe: secondary display requires both secondary_xres and "
                   "secondary_yres (got %ux%u)",
                   s->secondary_xres, s->secondary_yres);
        return;
    }
    if ((s->secondary_xres != 0 || s->secondary_yres != 0) && s->xres == 0 && s->yres == 0) {
        error_setg(errp, "goldfish_pipe: secondary display (%ux%u) requires primary xres and yres",
                   s->secondary_xres, s->secondary_yres);
        return;
    }

    if (s->xres != 0 || s->yres != 0) {
        if (s->xres == 0 || s->yres == 0) {
            error_setg(errp, "goldfish_pipe: graphics requires both xres and yres (got %ux%u)",
                       s->xres, s->yres);
            return;
        }
        if (s->xres > GOLDFISH_GPU_MAX_FRAME_DIM || s->yres > GOLDFISH_GPU_MAX_FRAME_DIM) {
            error_setg(errp, "goldfish_pipe: primary display %ux%u exceeds maximum dimension %u",
                       s->xres, s->yres, GOLDFISH_GPU_MAX_FRAME_DIM);
            return;
        }
        if (s->secondary_xres > GOLDFISH_GPU_MAX_FRAME_DIM ||
            s->secondary_yres > GOLDFISH_GPU_MAX_FRAME_DIM) {
            error_setg(errp, "goldfish_pipe: secondary display %ux%u exceeds maximum dimension %u",
                       s->secondary_xres, s->secondary_yres, GOLDFISH_GPU_MAX_FRAME_DIM);
            return;
        }
        if (s->guest_api_level == 0) {
            error_setg(errp,
                       "goldfish_pipe: graphics requires the guest_api_level property (the AVD's "
                       "Android API level), got 0");
            return;
        }
        if (dev->id == NULL) {
            dev->id = g_strdup(GOLDFISH_LEGACY_GPU_ID);
        }
        s->displays[0].con = graphic_console_init(dev, 0, &goldfish_pipe_gfx_ops, s);
        if (s->displays[0].con == NULL) {
            error_setg(errp, "goldfish_pipe: failed to create display console");
            return;
        }
        DisplaySurface* ds = qemu_create_displaysurface((int)s->xres, (int)s->yres);
        dpy_gfx_replace_surface(s->displays[0].con, ds);

        const bool has_secondary = (s->secondary_xres != 0 && s->secondary_yres != 0);
        if (has_secondary) {
            // Keep QEMU's placeholder surface on head=1 until the first outer-display frame
            // arrives: grpc_display / MultiDisplayImpl sizes QemuDisplay(1) from
            // hw_displayRegion_0_1_{width,height} while reporting kNoContent until real
            // pixels are presented, matching virtio-gpu-gfxstream with max_outputs=2.
            s->displays[1].con = graphic_console_init(dev, 1, &goldfish_pipe_gfx_ops, s);
            if (s->displays[1].con == NULL) {
                error_setg(errp, "goldfish_pipe: failed to create secondary display console");
                return;
            }
        }

        qemu_mutex_init(&s->frame_mu);
        s->frame_mu_inited = true;
        s->present_bh = qemu_bh_new(goldfish_pipe_present_bh, s);
        s->flush_timer = timer_new_ms(QEMU_CLOCK_REALTIME, goldfish_pipe_flush_timer_cb, s);
        qatomic_set(&s->graphics_active, true);
        g_active_graphics_pipe = s;

        char err_buf[512] = {0};
        if (!GoldfishGpuCoreStart(s->xres, s->yres, s->secondary_xres, s->secondary_yres,
                                  s->guest_api_level, s->renderer_features, s->snapshot_directory,
                                  goldfish_gpu_on_frame_posted, s, err_buf, sizeof(err_buf))) {
            goldfish_pipe_stop_graphics(s, /*for_process_exit=*/false);
            error_setg(errp, "goldfish_pipe: failed to start graphics: %s", err_buf);
            return;
        }
        // The GPU owns its snapshot state: this registers the "goldfish_gpu_begin"
        // (MIG_PRI_IOMMU, first) and "goldfish_gpu_end" (MIG_PRI_LOW, last) VMState sections that
        // bookend the MIG_PRI_DEFAULT devices (goldfish_address_space, goldfish_sync,
        // goldfish_pipe).
        goldfish_gpu_vmstate_register();
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &goldfish_pipe_iomem_ops, s, TYPE_GOLDFISH_PIPE,
                          GOLDFISH_PIPE_IOMEM_SIZE);
    sysbus_init_mmio(sbdev, &s->iomem);
    sysbus_init_irq(sbdev, &s->irq);
    s->irq_level = 0;
    s->irq_bh = qemu_bh_new(goldfish_pipe_irq_bh, s);

    struct GoldfishDeviceEnv env;
    memset(&env, 0, sizeof(env));
    env.memory.opaque = s;
    env.memory.read = goldfish_pipe_guest_mem_read;
    env.memory.write = goldfish_pipe_guest_mem_write;
    env.irq.opaque = s;
    env.irq.set_level = goldfish_pipe_irq_set_level;

    s->core = GoldfishPipeCoreCreate(&env);
}

static void goldfish_pipe_unrealize(DeviceState* dev) {
    struct GoldfishPipeState* s = GOLDFISH_PIPE(dev);
    // A sysbus device is only unrealized by QEMU's exit-time deletable_cleanup()
    // (see add_deletable_object() in reset_hold), so this is the process-exit path.
    goldfish_pipe_stop_graphics(s, /*for_process_exit=*/true);
    if (s->core.destroy != NULL) {
        s->core.destroy(s->core.core);
        memset(&s->core, 0, sizeof(s->core));
    }
    if (s->irq_bh != NULL) {
        qemu_bh_delete(s->irq_bh);
        s->irq_bh = NULL;
    }
    g_free(s->renderer_features);
    s->renderer_features = NULL;
    g_free(s->snapshot_directory);
    s->snapshot_directory = NULL;
}

static void goldfish_pipe_clear_pending_frames(struct GoldfishPipeState* s) {
    if (s == NULL || !s->frame_mu_inited) {
        return;
    }
    qemu_mutex_lock(&s->frame_mu);
    for (size_t i = 0; i < GOLDFISH_PIPE_MAX_DISPLAYS; ++i) {
        s->displays[i].frame_dirty = false;
    }
    qemu_mutex_unlock(&s->frame_mu);
}

static void goldfish_pipe_reset_hold(Object* obj, ResetType type) {
    (void)type;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    if (!s->deletable_registered) {
        // Register in reset_hold (after CLI -device creation) rather than
        // realize (board init) so LIFO deletable_cleanup() tears down
        // goldfish_pipe before goldfish-address-space and avdstart.
        s->deletable_registered = true;
        add_deletable_object(obj);
    }
    goldfish_pipe_clear_pending_frames(s);
    assert(s->core.reset != NULL);
    s->core.reset(s->core.core);
}

static int goldfish_pipe_pre_load(void* opaque) {
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    goldfish_pipe_clear_pending_frames(s);
    const char* snap_name = qemu_snapshot_in_flight_name();
    return GoldfishPipeCorePreLoad(s->core.core, snap_name) ? 0 : -1;
}

static int goldfish_pipe_post_load(void* opaque, int version_id) {
    (void)version_id;
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)opaque;
    const char* snap_name = qemu_snapshot_in_flight_name();
    return GoldfishPipeCorePostLoad(s->core.core, snap_name) ? 0 : -1;
}

static int goldfish_pipe_vmstate_put(QEMUFile* f, void* pv, size_t size, const VMStateField* field,
                                     JSONWriter* vmdesc) {
    (void)size;
    (void)field;
    (void)vmdesc;
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)pv;
    const char* snap_name = qemu_snapshot_in_flight_name();
    return GoldfishPipeCoreSave(s->core.core, snap_name, f) ? 0 : -1;
}

static int goldfish_pipe_vmstate_get(QEMUFile* f, void* pv, size_t size,
                                     const VMStateField* field) {
    (void)size;
    (void)field;
    struct GoldfishPipeState* s = (struct GoldfishPipeState*)pv;
    const char* snap_name = qemu_snapshot_in_flight_name();
    return GoldfishPipeCoreLoad(s->core.core, snap_name, f) ? 0 : -1;
}

static const VMStateInfo goldfish_pipe_vmstate_info = {
    .name = "goldfish_pipe_blob",
    .get = goldfish_pipe_vmstate_get,
    .put = goldfish_pipe_vmstate_put,
};

static const VMStateDescription goldfish_pipe_vmsd = {
    .name = "goldfish_pipe",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = goldfish_pipe_pre_load,
    .post_load = goldfish_pipe_post_load,
    .fields =
            (const VMStateField[]){
                {
                    .name = "state",
                    .version_id = 1,
                    .size = 0,
                    .info = &goldfish_pipe_vmstate_info,
                    .flags = VMS_SINGLE,
                    .offset = 0,
                },
                VMSTATE_END_OF_LIST(),
            },
};

static void goldfish_pipe_get_xres(Object* obj, Visitor* v, const char* name, void* opaque,
                                   Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    uint32_t val = s->xres;
    visit_type_uint32(v, name, &val, errp);
}

static void goldfish_pipe_set_xres(Object* obj, Visitor* v, const char* name, void* opaque,
                                   Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    visit_type_uint32(v, name, &s->xres, errp);
}

static void goldfish_pipe_get_yres(Object* obj, Visitor* v, const char* name, void* opaque,
                                   Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    uint32_t val = s->yres;
    visit_type_uint32(v, name, &val, errp);
}

static void goldfish_pipe_set_yres(Object* obj, Visitor* v, const char* name, void* opaque,
                                   Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    visit_type_uint32(v, name, &s->yres, errp);
}

static void goldfish_pipe_get_secondary_xres(Object* obj, Visitor* v, const char* name,
                                             void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    uint32_t val = s->secondary_xres;
    visit_type_uint32(v, name, &val, errp);
}

static void goldfish_pipe_set_secondary_xres(Object* obj, Visitor* v, const char* name,
                                             void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    visit_type_uint32(v, name, &s->secondary_xres, errp);
}

static void goldfish_pipe_get_secondary_yres(Object* obj, Visitor* v, const char* name,
                                             void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    uint32_t val = s->secondary_yres;
    visit_type_uint32(v, name, &val, errp);
}

static void goldfish_pipe_set_secondary_yres(Object* obj, Visitor* v, const char* name,
                                             void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    visit_type_uint32(v, name, &s->secondary_yres, errp);
}

static void goldfish_pipe_get_guest_api_level(Object* obj, Visitor* v, const char* name,
                                              void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    uint32_t val = s->guest_api_level;
    visit_type_uint32(v, name, &val, errp);
}

static void goldfish_pipe_set_guest_api_level(Object* obj, Visitor* v, const char* name,
                                              void* opaque, Error** errp) {
    (void)opaque;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    visit_type_uint32(v, name, &s->guest_api_level, errp);
}

static char* goldfish_pipe_get_renderer_features(Object* obj, Error** errp) {
    (void)errp;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    return g_strdup(s->renderer_features ? s->renderer_features : "");
}

static void goldfish_pipe_set_renderer_features(Object* obj, const char* value, Error** errp) {
    (void)errp;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    g_free(s->renderer_features);
    s->renderer_features = g_strdup(value);
}

static char* goldfish_pipe_get_snapshot_directory(Object* obj, Error** errp) {
    (void)errp;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    return g_strdup(s->snapshot_directory ? s->snapshot_directory : "");
}

static void goldfish_pipe_set_snapshot_directory(Object* obj, const char* value, Error** errp) {
    (void)errp;
    struct GoldfishPipeState* s = GOLDFISH_PIPE(obj);
    g_free(s->snapshot_directory);
    s->snapshot_directory = g_strdup(value);
}

static void goldfish_pipe_class_init(ObjectClass* klass, const void* data) {
    (void)data;
    DeviceClass* dc = DEVICE_CLASS(klass);
    ResettableClass* rc = RESETTABLE_CLASS(klass);

    dc->realize = goldfish_pipe_realize;
    dc->unrealize = goldfish_pipe_unrealize;
    dc->desc = "goldfish pipe";
    dc->vmsd = &goldfish_pipe_vmsd;
    rc->phases.hold = goldfish_pipe_reset_hold;

    object_class_property_add(klass, "xres", "uint32", goldfish_pipe_get_xres,
                              goldfish_pipe_set_xres, NULL, NULL);
    object_class_property_add(klass, "yres", "uint32", goldfish_pipe_get_yres,
                              goldfish_pipe_set_yres, NULL, NULL);
    object_class_property_add(klass, "secondary_xres", "uint32", goldfish_pipe_get_secondary_xres,
                              goldfish_pipe_set_secondary_xres, NULL, NULL);
    object_class_property_add(klass, "secondary_yres", "uint32", goldfish_pipe_get_secondary_yres,
                              goldfish_pipe_set_secondary_yres, NULL, NULL);
    object_class_property_add(klass, "guest_api_level", "uint32", goldfish_pipe_get_guest_api_level,
                              goldfish_pipe_set_guest_api_level, NULL, NULL);
    object_class_property_add_str(klass, "renderer_features", goldfish_pipe_get_renderer_features,
                                  goldfish_pipe_set_renderer_features);
    object_class_property_add_str(klass, "snapshot_directory", goldfish_pipe_get_snapshot_directory,
                                  goldfish_pipe_set_snapshot_directory);
}

static const TypeInfo goldfish_pipe_info = {
    .name = TYPE_GOLDFISH_PIPE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct GoldfishPipeState),
    .class_init = goldfish_pipe_class_init,
};

void goldfish_pipe_register_types(void) {
    type_register_static(&goldfish_pipe_info);
}
