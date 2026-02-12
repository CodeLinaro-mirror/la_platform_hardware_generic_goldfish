# Component: AVD Universe

**Role:** Defines the "Universe" of the Android Virtual Device (AVD) state. It provides a collection of `ObservableValue`s that represent the state of various hardware and software components.
**Location:** `hardware/generic/goldfish/emulator/libs/avd_universe`
**Namespace:** `goldfish::avd_universe`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ClipboardChannel` | `:clipboard` | `.../clipboard/clipboard_data.h` | Bidirectional clipboard state (Host<->Guest). |
| `Battery` | `:battery` | `.../battery/battery_state.h` | Battery status and health state. |
| `ObservableFingerprintSensor` | `:fingerprint` | `.../fingerprint/fingerprint_sensor.h` | Fingerprint sensor touch events. |
| `GrpcNotificationChannel` | `:grpc_notification` | `.../grpc/grpc_notification_channel.h` | Channel for gRPC-based notifications. |
| `GuestStatus` | `:guest_status` | `.../guest_status/guest_status.h` | Status of the guest OS (booted, running, etc.). |
| `Location` | `:location` | `.../gps/location.h` | GPS/Location data state. |

## Critical Infrastructure
This component is primarily a set of **Data Transfer Objects (DTOs)** and **Reactive State Definitions**. It does not contain complex logic but rather defines the schema of the emulator's state.

## Dependencies
* **Core:** `//emulator/libs/eventing` (Provides `ObservableValue`).
* **External:** `@abseil-cpp` (Strings, Time).

## Threading Model
* **Thread Safety:** Relies on the thread safety of `goldfish::eventing::ObservableValue`.
* **Usage:** These observables are typically intended to be subscribed to by UI or System components and updated by hardware emulators.
