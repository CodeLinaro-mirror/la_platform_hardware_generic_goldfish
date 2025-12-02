// Copyright 2025 The Android Open Source Project
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

#include <stdbool.h>
#include <qemu/typedefs.h>

#include "qemu/osdep.h"
#include "qom/object.h"

typedef struct VirtIOInputHID VirtIOInputHID;

/**
 * @brief Structure to hold information about a virtio input device.
 *
 * This structure is used to pass information to the `find_virtio_device`
 * function and to store the result of the search. It contains the display
 * name, the head number, and a pointer to the found VirtIOInputHID device.
 */
typedef struct VirtioDeviceInfo {
    const char*
            display;  ///< The display name associated with the virtio input device (e.g., "gpu0").
    uint32_t head;         ///< The head number associated with the virtio input device.
    VirtIOInputHID* vhid;  ///< A pointer to the VirtIOInputHID structure representing the found
                           ///< virtio input device.
} VirtioDeviceInfo;

/**
 * @brief Finds a virtio input device within the QEMU object hierarchy.
 *
 * This function traverses the QEMU object tree, searching for a virtio input
 * device that matches the specified display and head. It is typically used
 * to locate the virtio input device associated with a particular display
 * head in a multi-display setup.
 *
 * @param obj The current object being visited in the object tree.
 * @param opaque A pointer to a VirtioDeviceInfo structure containing the
 *               display name and head to search for. The found VirtIOInputHID
 *               will be stored in this structure.
 * @return 1 if a matching virtio input device is found, 0 otherwise.
 */
int find_virtio_device(Object* obj, void* opaque);

/**
 * @brief Sends a generic evdev event to a virtio input device.
 *
 * This function sends an event of the specified type, code, and value to
 * the given virtio input device. It is used to inject input events into
 * the guest operating system through the virtio input interface.
 *
 * @param vhid A pointer to the VirtIOInputHID structure representing the
 *             virtio input device.
 * @param type The evdev event type (e.g., EV_KEY, EV_ABS).
 * @param code The evdev event code (e.g., KEY_A, ABS_X).
 * @param value The evdev event value.
 */
void virtio_input_send_evdev(VirtIOInputHID* vhid, uint16_t type, uint16_t code, uint32_t value);
