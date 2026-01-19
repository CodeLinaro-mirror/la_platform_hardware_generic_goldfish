# Component: gRPC Emulator Controller

**Role:** The primary gRPC service for remote control and observation of the Android Emulator.
**Location:** `hardware/generic/goldfish/emulator/grpc/services/emulator_controller`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `getEmulatorController()` | `.../server:emulator-service` | `.../emulator_service.h` | Factory to create the aggregate controller service. |

## Critical Infrastructure
* **Service Aggregation:** `EmulatorControllerImpl` acts as a facade, delegating calls to specialized internal implementations:
    *   **VM State:** Start, Stop, Pause, Resume (via `vminterface`).
    *   **Graphics:** Screenshots and display streaming (via `display` and `imaging` libs).
    *   **Sensors:** Accelerometer, Gyro, GPS (via `physics` and `sensors` libs).
    *   **Input:** Keyboard, Mouse, Touch injection (via `virtio-input-android` and QEMU console).
    *   **Clipboard:** Bidirectional sync and streaming (via `avd_universe`).
    *   **Notifications:** Push notifications from the guest to the gRPC client.
* **Callback API:** Extensively uses gRPC's callback-based API (`WithCallbackMethod_...`) for streaming endpoints to maximize concurrency and minimize resource usage.

## Dependencies
* **Core Logic:** `//emulator/plugin/avd:info`, `//emulator/plugin/vminterface`.
* **HAL Support:** `//emulator/hal/...`
* **Common Libs:** `//emulator/libs/display`, `//emulator/libs/fps_calculator`.
* **Async:** `//emulator/grpc/async`, `//emulator/grpc/event-stream`.

## Threading Model
* **gRPC Thread Pool:** Requests are handled by gRPC threads.
* **Marshalling:** Operations that affect QEMU state (e.g., VM start/stop, input injection) are marshalled to the QEMU main loop using the `EventLoop` provided during initialization.
* **Streams:** Bi-directional streams (like `streamInputEvent`) use reactors to handle asynchronous message processing without blocking.
