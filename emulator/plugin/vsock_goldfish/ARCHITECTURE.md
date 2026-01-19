# Component: VSOCK Goldfish Plugin

**Role:** Implements the backend for the `virtio-vsock` device in QEMU, enabling high-performance socket-like communication between the Host and Guest.
**Location:** `hardware/generic/goldfish/emulator/plugin/vsock_goldfish`
**Namespace:** `goldfish::vsock` (C++), `goldfish_virtio_vsock_*` (C)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GoldfishVirtIOVSockDevAPI` | `:goldfish_vsock_hdrs` | `.../vsock_low_level.h` | Structure of function pointers passed from QEMU to this plugin. |
| `Connect` / `Listen` | `:goldfish_vsock_hdrs` | `.../connect.h`, `.../listen.h` | APIs for Host services to open VSOCK connections to the Guest. |

## Critical Infrastructure
* **GoldfishVirtioVsockDevice:** The singleton C++ class that manages the state of all VSOCK connections. It maps `(guest_port, host_port)` tuples to `VsockStream` instances.
* **VsockStream:** Represents a single active connection. It implements `ISocket` to interface with the `cable` library (used by host services).
* **QEMU Bridge:** Exposes C-compatible functions (`goldfish_virtio_vsock_impl_*`) called by QEMU's virtual device when IRQs/Packets happen.

## Architecture
The system acts as a bridge:
1.  **Guest -> Host:** The virtio driver in the guest kernel writes packets to the virtqueue. QEMU catches this and calls `goldfish_virtio_vsock_accept_guest_to_host_rw`. This plugin forwards the data to the corresponding `Plug` (Host Service).
2.  **Host -> Guest:** A Host Service writes to its `Socket`. This plugin buffers the data in `hostToGuestBuf` and triggers `goldfish_virtio_vsock_handle_host_to_guest`, which pushes it to the virtqueue (and notifies the guest via IRQ).

## Dependencies
* **Core:** `@qemu` (Virtio headers), `//emulator/libs/SocketBuffer` (Buffering).
* **HAL:** `//emulator/hal/connector` (Cable abstraction: `ISocket`, `IPlug`).
* **Serialization:** `//emulator/libs/archive` (Snapshot support).

## Threading Model
* **QEMU Main Loop:** The critical path (`realize`, `onPacketReceive`, `sendPackets`) is executed on the QEMU main thread (or AioContext).
* **Thread Safety:** The `GoldfishVirtioVsockDevice` uses a `std::recursive_mutex` (`mStateMutex`) to protect its stream map, allowing Host Services to call `SendAsync` or `Connect` from arbitrary threads safely.
