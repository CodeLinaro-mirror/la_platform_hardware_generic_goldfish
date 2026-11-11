// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/legacy/goldfish_events_low_level.h"

#include "emulator/plugin/virtio-input-android/virtio_input_android_config.h"
#include "goldfish/legacy/goldfish_events.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "standard-headers/linux/input.h"
// IWYU pragma: end_keep
// clang-format on

#include "android/base/logging/abseil_log_bridge.h"

#define TYPE_GOLDFISH_EVDEV "goldfish-events"
OBJECT_DECLARE_SIMPLE_TYPE(GoldfishEvDevState, GOLDFISH_EVDEV)

#define MAX_EVENTS (256 * 16 * 4)

// Non-zero high bit distinguishes goldfish-events active generations from
// virtio-input-android active generations.
#define GOLDFISH_EVENTS_GENERATION_TAG (1ULL << 62)

static const char kDeviceName[] = "qwerty2";

enum {
    REG_READ = 0x00,
    REG_SET_PAGE = 0x00,
    REG_LEN = 0x04,
    REG_DATA = 0x08,

    PAGE_NAME = 0x00000,
    PAGE_EVBITS = 0x10000,
    PAGE_ABSDATA = 0x20000 | EV_ABS,
};

enum {
    STATE_INIT = 0,
    STATE_LIVE,
    STATE_BUFFERED,
};

typedef struct AbsAxisInfo {
    int32_t min;
    int32_t max;
    uint32_t fuzz;
    uint32_t flat;
} AbsAxisInfo;

struct GoldfishEvDevState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    struct {
        uint8_t* bits;
        int len;
    } ev_bits[EV_MAX + 1];
    AbsAxisInfo abs_info[ABS_MAX];

    uint32_t page;
    uint32_t events[MAX_EVENTS];
    uint32_t first;
    uint32_t last;
    uint32_t state;

    bool logged_queue_full;
    uint64_t active_generation;
};

static GoldfishEvDevState* s_evdev = NULL;

static void pulse_irq(qemu_irq irq) {
    qemu_irq_lower(irq);
    qemu_irq_raise(irq);
}

static int get_page_length(const GoldfishEvDevState* s) {
    if (s->page == PAGE_NAME) {
        return (int)(sizeof(kDeviceName) - 1);
    }
    if (s->page >= PAGE_EVBITS && s->page <= PAGE_EVBITS + EV_MAX) {
        return s->ev_bits[s->page - PAGE_EVBITS].len;
    }
    if (s->page == PAGE_ABSDATA) {
        return (int)sizeof(s->abs_info);
    }
    return 0;
}

static int read_page_data(const GoldfishEvDevState* s, uint32_t offset) {
    const int page_len = get_page_length(s);
    if (page_len <= 0 || offset >= (uint32_t)page_len) {
        return 0;
    }
    if (s->page == PAGE_NAME) {
        return (uint8_t)kDeviceName[offset];
    }
    if (s->page >= PAGE_EVBITS && s->page <= PAGE_EVBITS + EV_MAX) {
        return s->ev_bits[s->page - PAGE_EVBITS].bits[offset];
    }
    if (s->page == PAGE_ABSDATA) {
        const int32_t* words = (const int32_t*)s->abs_info;
        return words[offset / sizeof(int32_t)];
    }
    return 0;
}

static unsigned dequeue_event_word(GoldfishEvDevState* s) {
    if (s->first == s->last) {
        return 0;
    }

    const unsigned word = s->events[s->first];
    s->first = (s->first + 1) & (MAX_EVENTS - 1);

    const uint32_t remaining = (s->last - s->first) & (MAX_EVENTS - 1);
    if (remaining == 0) {
        qemu_set_irq(s->irq, 0);
    } else if (remaining >= 3 && (remaining % 3) == 0) {
        // Re-assert the IRQ after each 3-word (type, code, value) event when
        // more events remain (required for edge-triggered x86 ACPI GSI).
        pulse_irq(s->irq);
    }
    return word;
}

static void enqueue_event(GoldfishEvDevState* s, uint32_t type, uint32_t code, int value) {
    const bool need_bql = !bql_locked();
    if (need_bql) {
        bql_lock();
    }

    const uint32_t used = (s->last - s->first) & (MAX_EVENTS - 1);
    if (used + 3 >= MAX_EVENTS) {
        const bool log_drop = !s->logged_queue_full;
        s->logged_queue_full = true;
        if (need_bql) {
            bql_unlock();
        }
        if (log_drop) {
            ALOGW("goldfish-events queue full (%d words), dropping event (%u, %u, %d); further "
                  "drops are not logged until reset",
                  MAX_EVENTS, type, code, value);
        }
        return;
    }

    s->events[s->last] = type;
    s->last = (s->last + 1) & (MAX_EVENTS - 1);
    s->events[s->last] = code;
    s->last = (s->last + 1) & (MAX_EVENTS - 1);
    s->events[s->last] = (uint32_t)value;
    s->last = (s->last + 1) & (MAX_EVENTS - 1);

    if (s->state == STATE_LIVE) {
        pulse_irq(s->irq);
    } else {
        s->state = STATE_BUFFERED;
    }

    if (need_bql) {
        bql_unlock();
    }
}

static int send_event_to_device(int type, int code, int value) {
    if (s_evdev == NULL) {
        return -1;
    }
    // `qwerty.kl` maps Linux key 102 (`KEY_HOME`) to `KEYCODE_HOME`.
    if (type == EV_KEY && code == KEY_HOMEPAGE) {
        code = KEY_HOME;
    }
    enqueue_event(s_evdev, (uint32_t)type, (uint32_t)code, value);
    return 0;
}

static void set_event_bit(GoldfishEvDevState* s, int type, int bit) {
    const int byte_index = bit / 8;
    if (byte_index >= s->ev_bits[type].len) {
        const int new_len = byte_index + 1;
        uint8_t* bits = g_malloc0(new_len);
        if (s->ev_bits[type].bits != NULL) {
            memcpy(bits, s->ev_bits[type].bits, s->ev_bits[type].len);
            g_free(s->ev_bits[type].bits);
        }
        s->ev_bits[type].bits = bits;
        s->ev_bits[type].len = new_len;
    }
    s->ev_bits[type].bits[byte_index] |= (uint8_t)(1U << (bit & 7));
}

static void set_event_bit_range(GoldfishEvDevState* s, int type, int first_bit, int last_bit) {
    for (int bit = first_bit; bit <= last_bit; ++bit) {
        set_event_bit(s, type, bit);
    }
}

static uint64_t goldfish_evdev_read(void* opaque, hwaddr offset, unsigned size) {
    (void)size;
    GoldfishEvDevState* s = (GoldfishEvDevState*)opaque;

    // The guest driver reads REG_LEN of PAGE_ABSDATA at the end of events_probe()
    // right before registering its IRQ and input_dev.
    if (offset == REG_LEN && s->page == PAGE_ABSDATA) {
        if (s->state == STATE_BUFFERED) {
            qemu_set_irq(s->irq, 1);
        }
        if (s->state != STATE_LIVE) {
            s->state = STATE_LIVE;
            const uint64_t gen = (++s->active_generation) | GOLDFISH_EVENTS_GENERATION_TAG;
            goldfish_events_notify_active_changed(true, gen);
        }
    }

    switch (offset) {
    case REG_READ:
        return dequeue_event_word(s);
    case REG_LEN:
        return (uint64_t)get_page_length(s);
    default:
        if (offset >= REG_DATA) {
            return (uint64_t)read_page_data(s, (uint32_t)(offset - REG_DATA));
        }
        qemu_log_mask(LOG_GUEST_ERROR, "goldfish-events read: bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void goldfish_evdev_write(void* opaque, hwaddr offset, uint64_t val, unsigned size) {
    (void)size;
    GoldfishEvDevState* s = (GoldfishEvDevState*)opaque;
    if (offset == REG_SET_PAGE) {
        s->page = (uint32_t)val;
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "goldfish-events write: bad offset 0x%x\n", (int)offset);
}

static const MemoryRegionOps goldfish_evdev_ops = {
    .read = goldfish_evdev_read,
    .write = goldfish_evdev_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void goldfish_evdev_init(Object* obj) {
    GoldfishEvDevState* s = GOLDFISH_EVDEV(obj);
    SysBusDevice* sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &goldfish_evdev_ops, s, "goldfish-events", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void goldfish_evdev_realize(DeviceState* dev, Error** errp) {
    (void)errp;
    GoldfishEvDevState* s = GOLDFISH_EVDEV(dev);

    if (dev->id == NULL) {
        dev->id = g_strdup(TYPE_GOLDFISH_EVDEV);
    }

    // Advertise keyboard keys [1, 0xff] and Android hardware keys above KEY_OK
    // (0x160) while leaving the whole button range [0x100, 0x160] clear, which
    // covers [BTN_MISC, BTN_MOUSE) and [BTN_JOYSTICK, BTN_DIGI): any bit in those
    // ranges makes Android's EventHub classify `qwerty2` as
    // INPUT_DEVICE_CLASS_JOYSTICK instead of INPUT_DEVICE_CLASS_TOUCH_MT.
    set_event_bit(s, EV_SYN, EV_KEY);
    set_event_bit_range(s, EV_KEY, 1, 0xff);
    set_event_bit_range(s, EV_KEY, KEY_OK + 1, 0x1ff);
    set_event_bit(s, EV_KEY, KEY_APPSELECT);

    size_t key_count = 0;
    const unsigned short* key_codes = virtio_input_android_ev_key_codes(&key_count);
    for (size_t i = 0; i < key_count; ++i) {
        set_event_bit(s, EV_KEY, key_codes[i]);
    }

    set_event_bit(s, EV_SYN, EV_ABS);
    size_t abs_count = 0;
    const unsigned short* abs_codes = virtio_input_android_ev_abs_codes(&abs_count);
    for (size_t i = 0; i < abs_count; ++i) {
        const unsigned short code = abs_codes[i];
        if (code >= ABS_MAX) {
            continue;
        }
        set_event_bit(s, EV_ABS, code);
        int32_t min_val = 0;
        int32_t max_val = 0;
        if (virtio_input_android_get_abs_range((uint8_t)code, &min_val, &max_val)) {
            s->abs_info[code].min = min_val;
            s->abs_info[code].max = max_val;
        }
    }

    set_event_bit(s, EV_SYN, EV_SW);
    const uint8_t sw_bits = virtio_input_android_ev_sw_bitmap();
    for (int bit = 0; bit < 8; ++bit) {
        if (sw_bits & (1U << bit)) {
            set_event_bit(s, EV_SW, bit);
        }
    }

    s_evdev = s;
    goldfish_events_set_send_handler(send_event_to_device);
}

static void goldfish_evdev_reset_hold(Object* obj, ResetType type) {
    (void)type;
    GoldfishEvDevState* s = GOLDFISH_EVDEV(obj);
    const bool was_live = (s->state == STATE_LIVE);
    s->page = 0;
    s->first = 0;
    s->last = 0;
    s->state = STATE_INIT;
    s->logged_queue_full = false;
    qemu_set_irq(s->irq, 0);
    if (was_live) {
        const uint64_t gen = (++s->active_generation) | GOLDFISH_EVENTS_GENERATION_TAG;
        goldfish_events_notify_active_changed(false, gen);
    }
}

static int goldfish_evdev_post_load(void* opaque, int version_id) {
    (void)version_id;
    GoldfishEvDevState* s = (GoldfishEvDevState*)opaque;
    if (s->first >= MAX_EVENTS || s->last >= MAX_EVENTS || s->state > STATE_BUFFERED) {
        return -EINVAL;
    }
    // `queued` may not be a multiple of 3: a snapshot can be taken while the
    // guest's events_interrupt() is between its three REG_READ loads, and
    // dequeue_event_word() resumes a partly read event correctly.
    const uint32_t queued = (s->last - s->first) & (MAX_EVENTS - 1);
    const bool is_live = (s->state == STATE_LIVE);
    const uint64_t gen = (++s->active_generation) | GOLDFISH_EVENTS_GENERATION_TAG;
    if (is_live && queued > 0) {
        pulse_irq(s->irq);
    } else {
        qemu_set_irq(s->irq, 0);
    }
    goldfish_events_notify_active_changed(is_live, gen);
    return 0;
}

static VMStateField goldfish_evdev_vmsd_fields[6];

static VMStateDescription vmstate_goldfish_evdev = {
    .name = "goldfish-events",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = goldfish_evdev_post_load,
    .fields = goldfish_evdev_vmsd_fields,
};

static void goldfish_evdev_class_init(ObjectClass* klass, const void* data) {
    (void)data;
    DeviceClass* dc = DEVICE_CLASS(klass);
    ResettableClass* rc = RESETTABLE_CLASS(klass);

    dc->realize = goldfish_evdev_realize;
    dc->vmsd = &vmstate_goldfish_evdev;
    dc->desc = "goldfish events";
    rc->phases.hold = goldfish_evdev_reset_hold;
}

static void goldfish_evdev_finalize(Object* obj) {
    GoldfishEvDevState* s = GOLDFISH_EVDEV(obj);
    if (s_evdev == s) {
        if (s->state == STATE_LIVE) {
            s->state = STATE_INIT;
            const uint64_t gen = (++s->active_generation) | GOLDFISH_EVENTS_GENERATION_TAG;
            goldfish_events_notify_active_changed(false, gen);
        }
        goldfish_events_set_send_handler(NULL);
        s_evdev = NULL;
    }
    for (int i = 0; i <= EV_MAX; ++i) {
        g_free(s->ev_bits[i].bits);
        s->ev_bits[i].bits = NULL;
        s->ev_bits[i].len = 0;
    }
}

static const TypeInfo goldfish_evdev_info = {
    .name = TYPE_GOLDFISH_EVDEV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GoldfishEvDevState),
    .instance_init = goldfish_evdev_init,
    .instance_finalize = goldfish_evdev_finalize,
    .class_init = goldfish_evdev_class_init,
};

void goldfish_events_register_types(void) {
    // b/445476051: VMStateField must be initialized at runtime on Windows because
    // VMSTATE_* macros reference data symbols imported from the QEMU executable.
    goldfish_evdev_vmsd_fields[0] = (VMStateField)VMSTATE_UINT32(page, GoldfishEvDevState);
    goldfish_evdev_vmsd_fields[1] =
            (VMStateField)VMSTATE_UINT32_ARRAY(events, GoldfishEvDevState, MAX_EVENTS);
    goldfish_evdev_vmsd_fields[2] = (VMStateField)VMSTATE_UINT32(first, GoldfishEvDevState);
    goldfish_evdev_vmsd_fields[3] = (VMStateField)VMSTATE_UINT32(last, GoldfishEvDevState);
    goldfish_evdev_vmsd_fields[4] = (VMStateField)VMSTATE_UINT32(state, GoldfishEvDevState);
    goldfish_evdev_vmsd_fields[5] = (VMStateField)VMSTATE_END_OF_LIST();

    type_register_static(&goldfish_evdev_info);
}
