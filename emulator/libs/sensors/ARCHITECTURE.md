# Component: Sensors

**Role:** Implements the logic for emulated sensors, including foldable device states.
**Location:** `hardware/generic/goldfish/emulator/libs/sensors`
**Namespace:** `goldfish::sensors`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `PhysicalModel` | `@goldfish//emulator/libs/sensors:physical_model` | `.../physical_model.h` | Aggregates physical parameters and models to produce sensor data. |
| `FoldableModel` | `@goldfish//emulator/libs/sensors:physical_model` | `.../foldable_model.h` | Specialized logic for foldable device states (posture, hinge angles). |

## Critical Infrastructure
* **PhysicalModel:** The bridge between the raw physics engine (`emulator/libs/physics`) and the Android sensor stack. It updates sensor values based on device state.
* **Configuration:** Uses `HardwareConfig` to determine available sensors.

## Dependencies
* **Core:** `//emulator/libs/physics` (Math/Physics backend).
* **Config:** `//emulator/config:hardware_config`.
* **Events:** `//emulator/libs/eventing`.

## Threading Model
* **Thread Safe:** Model updates and reads should be synchronized.
