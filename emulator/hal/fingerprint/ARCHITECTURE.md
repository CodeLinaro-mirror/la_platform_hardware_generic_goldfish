# Component: Fingerprint HAL

**Role:** Simulates a fingerprint sensor, allowing the host to inject touch/enroll events into the guest.
**Location:** `hardware/generic/goldfish/emulator/hal/fingerprint`
**Namespace:** `goldfish::devices::fingerprint`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `FingerprintDevice` | `:fingerprint` | `.../fingerprint_device.h` | The HAL implementation. |

## Critical Infrastructure
* **Protocol:** Uses QEMUD to receive commands.
* **State Management:** Subscribes to `avd_universe::fingerprint::ObservableFingerprintSensor`.
* **Behavior:** When the observable is updated (e.g., UI triggers a fingerprint touch with an ID), the device sends the touch event string to the guest via QEMUD.

## Dependencies
* **Core:** `//emulator/hal/plug`.
* **Connector:** `//emulator/hal/connector:qemud` (Protocol).
* **State:** `//emulator/libs/avd_universe:fingerprint`.

## Threading Model
* **Thread Safe:** Inherits `HalPlug`. Updates from `avd_universe` are thread-safe observables.
