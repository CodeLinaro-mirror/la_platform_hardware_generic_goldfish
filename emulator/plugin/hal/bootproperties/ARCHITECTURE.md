# Component: Boot Properties HAL

**Role:** Serves boot properties (key-value pairs) to the guest OS during the boot process.
**Location:** `hardware/generic/goldfish/emulator/hal/bootproperties`
**Namespace:** `goldfish::devices::boot`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `IBootPropertiesDevice` | `:bootproperties` | `.../boot_properties_device.h` | Interface to register the device. |

## Critical Infrastructure
* **Protocol:** Uses the legacy `QEMUD` protocol (length-prefixed strings).
* **Behavior:** Responds to the `list` command by sending all registered properties followed by a null terminator.

## Dependencies
* **Core:** `//emulator/hal/plug` (HalPlug).
* **Connector:** `//emulator/hal/connector:qemud` (QEMUD framing).

## Threading Model
* **Thread Safe:** Inherits `HalPlug`, so all callbacks (`OnConnect`, `OnReceive`) run on the designated `EventLoop`.
