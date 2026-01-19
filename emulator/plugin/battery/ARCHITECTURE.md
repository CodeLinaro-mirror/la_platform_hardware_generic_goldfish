# Component: Battery Plugin

**Role:** Implements the Goldfish Battery QEMU device (`goldfish_battery`).
**Location:** `hardware/generic/goldfish/emulator/plugin/battery`
**Namespace:** `goldfish::devices::battery` (C++), `goldfish_battery_*` (C)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `goldfish_battery_register_types` | `:battery` | `.../goldfish_battery.h` | QEMU type registration. |
| `IBattery` | `:battery` | `include/goldfish/devices/battery/battery.h` | C++ interface to update battery state (capacity, status). |

## Critical Infrastructure
* **QEMU Device:** Implements a `SysBusDevice` with MMIO registers (`0x00` - `0x40`) and an IRQ.
* **MMIO:** Guest reads from registers to get voltage, capacity, health, etc.
* **Updates:** `Battery` class (C++) calls `goldfish_battery_set_prop` (C), which updates the internal struct and raises an IRQ to notify the guest kernel driver.

## Dependencies
* **Upstream:** `@qemu` (SysBus, IRQ, VMState).

## Threading Model
* **QEMU Context:** `goldfish_battery_read/write` run on the QEMU main thread (BQL held).
* **Updates:** `goldfish_battery_set_prop` grabs the BQL if not already held, making it thread-safe for external callers (e.g., from a Telnet console thread).
