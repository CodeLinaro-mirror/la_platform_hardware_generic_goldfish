# Component: Sensors HAL

**Role:** Bridges the simulated physical world (sensors) to the guest OS. **Location:**
`emulator/plugin/hal/sensors/` **Namespace:** `goldfish::devices::sensor`

## Integration Guide

| Class / Interface | Bazel Target | Header Path           | Description             |
| :---------------- | :----------- | :-------------------- | :---------------------- |
| `SensorDevice`    | `:sensors`   | `.../sensor_device.h` | The HAL implementation. |

## Critical Infrastructure

- **Protocol:** Uses the legacy QEMUD sensors protocol (`list`, `set-delay`, data frames).
- **Backing Store:** Instances of `goldfish::sensors::PhysicalModel` (from `emulator/libs/sensors`).
- **Behavior:**
  - **List:** Enumerates enabled sensors based on `HardwareConfig`.
  - **Data:** Polls the `PhysicalModel` at the requested rate and pushes updates to the guest.

## Dependencies

- **Core:** `//emulator/plugin/hal/plug`.
- **Connector:** `//emulator/plugin/hal/connector:qemud`.
- **Logic:** `//emulator/libs/sensors:physical_model`.

## Threading Model

- **Thread Safe:** Inherits `HalPlug`. Callbacks serialized on `EventLoop`.

## Flows & Guides

- [Sensor Update Flow](docs/sensor_update_flow.md)
