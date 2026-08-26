# Component: WebRTC QEMU Device Plugin

**Role:** Exposes the WebRTC in-process streaming device to QEMU as a QOM type.
**Location:** `hardware/generic/goldfish/emulator/plugin/webrtc`
**Namespace:** `TYPE_WEBRTC` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `webrtc_register_types` | `:webrtc` | `webrtc_device.h` | QEMU type registration for the WebRTC device plugin. |

## Critical Infrastructure
* **QEMU Device Lifecycle:** Instantiates the WebRTC device during QEMU machine initialization and hooks into the AVD display engine.
* **In-Process RTC Service:** Initializes `//emulator/plugin/grpc/services:in_process_rtc_service` within QEMU memory space.

## Dependencies
* **Core:** `//emulator/plugin/avd:impl`, `//emulator/plugin/grpc/services:in_process_rtc_service`.
* **QEMU:** `@qemu//:qemu-headers-exported`.

## Threading Model
* **QEMU Main Loop:** Device registration occurs on QEMU startup thread.
