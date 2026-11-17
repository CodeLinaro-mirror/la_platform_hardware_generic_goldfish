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
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/cdefs.h>

/**
 * @file goldfish_events.h
 * @brief QEMU-free hooks for the legacy `goldfish-events` ("qwerty2") device.
 *
 * Guest kernels below Android API 29 lack `CONFIG_VIRTIO_INPUT` and probe the
 * SysBus `goldfish-events` device instead. Display 0 touch and host keyboard
 * dispatch route through these lock-free hooks when the device is active.
 */

__BEGIN_DECLS

struct VirtIOInputHID;

typedef int (*GoldfishEventsSendFn)(int type, int code, int value);

void goldfish_events_set_primary_touch_device(struct VirtIOInputHID* device);
struct VirtIOInputHID* goldfish_events_get_primary_touch_device(void);

void goldfish_events_set_send_handler(GoldfishEventsSendFn handler);
void goldfish_events_notify_active_changed(bool active, uint64_t active_generation);

bool goldfish_events_is_active(void);
uint64_t goldfish_events_get_active_generation(void);
int goldfish_events_send(int type, int code, int value);

__END_DECLS
