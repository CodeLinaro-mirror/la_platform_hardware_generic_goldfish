# Component: HAL Plug

**Role:** A simplified, thread-safe abstraction for HAL devices to communicate over vsock.
**Location:** `hardware/generic/goldfish/emulator/hal/plug`
**Namespace:** `goldfish::devices`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `HalPlug` | `@goldfish//emulator/hal/plug:hal_plug` | `.../internal/hal_plug.h` | Base class for HAL implementations. Provides `onConnect`, `onReceive`, `OnClose`. |
| `HalSocket` | `@goldfish//emulator/hal/plug:hal_plug` | `.../internal/hal_plug.h` | Interface for sending data to the guest. |
| `HalPlugFactory` | `@goldfish//emulator/hal/plug:hal_plug_factory` | `.../hal_plug_factory.h` | Factory to create plugs and bind them to event loops. |

## Critical Infrastructure
* **Lifecycle Management:** Guarantees that `socket()` is only valid between `onConnect()` and `OnClose()`. Accessing it outside this window returns a safe "Null Object".
* **Thread Marshalling:** Ensures all `HalPlug` callbacks are executed on the *client-provided* `EventLoop`, isolating the HAL developer from QEMU's internal threading model.
* **Separation of Concerns:**
    *   `HalPlug`: Logic for the specific HAL (e.g., Sensors, Clipboard).
    *   `HalPlugFactory`: Boilerplate for connecting the Plug to the underlying `cable` and `vsock` machinery.

## Dependencies
* **Core:** `//emulator/libs/async` (EventLoop), `@abseil-cpp`.
* **Infrastructure:** `//emulator/hal/connector` (Cable/Socket abstractions), `//emulator/plugin/vsock_goldfish`.

## Threading Model
* **Thread Safe:** The primary design goal.
    *   **Sending:** `HalSocket::Send` is thread-safe and marshals data to the I/O thread.
    *   **Receiving:** `HalPlug::onReceive` is always serialized on the user's `EventLoop`.
    *   **Connection:** `onConnect` and `OnClose` are serialized on the user's `EventLoop`.
