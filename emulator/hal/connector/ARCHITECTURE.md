# Component: Connector

**Role:** The "Physical Layer" of the emulator's device interconnects, defining the abstract interface for data exchange (`cable`) and the registry for named services.
**Location:** `hardware/generic/goldfish/emulator/hal/connector`
**Namespace:** `goldfish::devices`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `ISocket` | `:cable` | `.../cable/cable.h` | Abstract interface for *sending* data. |
| `IPlug` | `:cable` | `.../cable/cable.h` | Abstract interface for *receiving* data. |
| `IConnectorRegistry` | `:connector_registry` | `.../connector_registry.h` | Interface to register named services (devices) reachable by the guest. |

## Critical Infrastructure
* **The Cable Metaphor:**
    *   **Socket:** The male end. Sends data out.
    *   **Plug:** The female end. Receives data in.
    *   **Connection:** A Plug is "plugged into" a Socket. The Socket calls `Plug::OnReceive`. The Plug holds a reference to the Socket to call `Socket::SendAsync`.
* **Connector Registry:** The central lookup table where host services register themselves by name (e.g., "sensors", "clipboard"). When the guest connects to a specific port/service, the registry invokes the corresponding factory.

## Threading Model
* **Legacy vs Modern:**
    *   **Legacy (`RegisterDevice`):** `IPlug` implementations run directly on the I/O thread (often QEMU's main loop). This is dangerous and prone to blocking the UI.
    *   **Modern (`RegisterHalDevice`):** Uses the `HalPlug` wrapper (from `//emulator/hal/plug`) to transparently marshal all events to a dedicated `EventLoop`, ensuring thread safety.

## Dependencies
* **Core:** `//emulator/hal/plug` (For modern HAL support).
* **Broadcasting:** `//emulator/libs/broadcasting` (PingTopic).
