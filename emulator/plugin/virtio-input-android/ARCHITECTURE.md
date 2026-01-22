# Component: Virtio Input Android Plugin

**Role:** Implements a `virtio-input` device specialized for Android multi-touch support.
**Location:** `hardware/generic/goldfish/emulator/plugin/virtio-input-android`
**Namespace:** `TYPE_VIRTIO_INPUT_ANDROID_HID` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `virtio_input_android_register_types` | `:virtio-input-android` | `.../virtio_input_android.h` | QEMU type registration. |

## Critical Infrastructure
* **Translation:** Bridges QEMU's internal `InputEvent` (generic mouse/tablet events) to the Linux `input_event` (evdev) protocol used by `virtio-input`.
* **Multi-Touch:** Implements the Linux Multi-Touch Protocol B (slots). It maintains the state of active touch points (`tracked_pointers`) and generates `ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`, and `ABS_MT_POSITION_X/Y` events.
* **Naming:** Generates unique device names like `virtio_input_multi_touch_N` so the guest can distinguish between multiple touchscreens (e.g., for foldable devices or multi-display).

## Dependencies
* **Upstream:** `@qemu` (Virtio Input, Input subsystem).

## Threading Model
* **QEMU Context:** Event handlers (`virtio_input_handle_event`) run on the QEMU main thread (BQL held).
