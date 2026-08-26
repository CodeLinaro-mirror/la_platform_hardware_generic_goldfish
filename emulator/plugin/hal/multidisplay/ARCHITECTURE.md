# Component: Multi-Display HAL

**Role:** Manages dynamic secondary and fold/unfold display creation, hot-plugging, resolution changes, and guest UI routing.
**Location:** `hardware/generic/goldfish/emulator/plugin/hal/multidisplay`
**Namespace:** `goldfish::devices`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `MultiDisplayDevice` | `:multidisplay` | `include/goldfish/devices/multidisplay/multidisplay_device.h` | QEMU device and HAL driver managing multi-display layout and lifecycle. |

## Critical Infrastructure
* **Dynamic Display Allocation:** Communicates with guest OS via QEMUD service or virtio-gpu to announce addition/removal of auxiliary displays.
* **Display Configuration:** Configures display IDs, DPI, width, height, and display flags (e.g. secondary automotive displays or foldable panels).

## Dependencies
* **HAL Connector:** `//emulator/plugin/hal/connector:cable`, `//emulator/plugin/hal/connector:qemud`.
* **AVD Info:** `//emulator/plugin/avd:info`, `//emulator/plugin/avd:gralloc_impl`.
* **Async & VM:** `//emulator/libs/async:async_api`, `//emulator/plugin/vminterface`.

## Threading Model
* **Thread Safe:** State updates are protected by internal mutex and dispatched to QEMU main loop when reconfiguring virtual hardware.
