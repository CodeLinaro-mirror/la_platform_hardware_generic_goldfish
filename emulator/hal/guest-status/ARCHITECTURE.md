# Component: Guest Status HAL

**Role:** Tracks the lifecycle of the guest OS (Booting, Boot Completed, Shutdown).
**Location:** `hardware/generic/goldfish/emulator/hal/guest-status`
**Namespace:** `goldfish::devices::guest_status`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GuestStatusDevice` | `:guest-status` | `.../guest_status_device.h` | The HAL implementation. |

## Critical Infrastructure
* **Protocol:** Uses QEMUD to receive status updates from the guest (e.g., `boot-completed`).
* **State Management:** Updates `avd_universe::guest_status::GuestStatus` so other components (like UI or Metrics) know the machine state.
* **VM Interface:** Can trigger a snapshot load upon boot completion if configured.

## Dependencies
* **Core:** `//emulator/hal/plug`.
* **Connector:** `//emulator/hal/connector:qemud`.
* **State:** `//emulator/libs/avd_universe:guest_status`.
* **Plugin:** `//emulator/plugin/vminterface` (Snapshot triggers).

## Threading Model
* **Thread Safe:** Inherits `HalPlug`.
