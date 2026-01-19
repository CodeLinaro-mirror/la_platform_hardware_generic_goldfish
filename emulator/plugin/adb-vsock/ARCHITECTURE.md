# Component: ADB VSOCK Plugin

**Role:** Specialized QEMU device (`virtio-goldfish-adb`) that bridges the guest's ADB daemon (adbd) to the host ADB server.
**Location:** `hardware/generic/goldfish/emulator/plugin/adb-vsock`
**Namespace:** `goldfish::adb_device` (C++), `TYPE_ADB_VSOCK_DEVICE` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `adb_device_register_types` | `:adb-vsock` | `.../adb_device.h` | QEMU type registration entry point. |

## Critical Infrastructure
* **VSOCK Port Forwarding:** Inherits from `VSockFwdDev` (see `emulator/plugin/vsock_goldfish`). It forwards guest port 5555 (adbd) to a dynamically allocated host port.
* **ADB Server Notification:** Upon connection, it notifies the local ADB server (`adb connect localhost:<port>`) so the emulator appears in `adb devices`.
* **Monitoring:** Supports an optional "monitor" property to sniff and log ADB protocol traffic using `AdbLogger`.

## Dependencies
* **Upstream:** `@qemu` (QOM/Device model).
* **Plugin:** `//emulator/plugin/vsock_goldfish` (Base VSOCK port forwarding logic).
* **ADB:** `//emulator/adb/host`, `//emulator/adb/logger`.

## Threading Model
* **QEMU Context:** `realize` and connection callbacks run on the QEMU main loop.
