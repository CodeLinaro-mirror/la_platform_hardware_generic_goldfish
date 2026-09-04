# Component: Vehicle HAL (VHAL)

**Role:** Simulates Android Automotive Vehicle Hardware Abstraction Layer (VHAL) properties (speed, gear, HVAC, night mode, fuel/battery level).
**Location:** `hardware/generic/goldfish/emulator/plugin/hal/vehicle`
**Namespace:** `goldfish::devices`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `VehicleDevice` | `:vehicle` | `include/goldfish/devices/vehicle/vehicle_device.h` | Vehicle HAL plugin device communicating with guest Car Service. |

## Critical Infrastructure
* **VHAL Property Protocol:** Marshals vehicle property read/write requests over vsock / QEMUD channel to the Android Car Service.
* **State Synchronization:** Integrates with `avd_universe::VehicleState` to reflect automotive sensor changes injected via gRPC or UI.

## Dependencies
* **State Model:** `//emulator/plugin/avd_universe:vehicle`.
* **HAL Connector:** `//emulator/plugin/hal/connector:connector_registry`.
* **Protos:** `@aemu//protos/services/incubating/car:vehicle_service_cc_proto`.

## Threading Model
* **Thread Safe:** Property injection and listener notifications are synchronized.
