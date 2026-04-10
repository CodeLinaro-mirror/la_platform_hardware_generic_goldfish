# Component: GPS HAL

**Role:** Provides location (GPS) data to the guest.
**Location:** `hardware/generic/goldfish/emulator/hal/gps`
**Namespace:** `goldfish::devices::gps`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GpsDevice` | `:gps` | `.../gps_device.h` | The HAL implementation. |

## Critical Infrastructure
* **Protocol:** Uses the legacy QEMUD GPS protocol.
* **State Management:** Subscribes to `avd_universe::gps::Location` to receive location updates.
* **Behavior:** When the location in `avd_universe` updates (e.g., via UI or Telnet), the device formats it into an NMEA-like or internal string format and sends it to the guest.

## Dependencies
* **Core:** `//emulator/hal/plug`.
* **Connector:** `//emulator/hal/connector:qemud`.
* **State:** `//emulator/libs/avd_universe:location`.

## Threading Model
* **Thread Safe:** Inherits `HalPlug`. Callbacks serialized on `EventLoop`.
