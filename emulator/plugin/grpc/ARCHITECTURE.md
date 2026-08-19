# Component: gRPC Plugin

**Role:** Exposes the Emulator Controller gRPC service (`EmulatorControllerService`) as a QEMU device.
**Location:** `hardware/generic/goldfish/emulator/plugin/grpc`
**Namespace:** `goldfish::grpc` (C++), `TYPE_GRPC` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `grpc_register_types` | `:grpc` | `.../grpc.h` | QEMU type registration. |

## Critical Infrastructure
* **Service Hosting:** Launches the `EmulatorControllerService` and WebRTC `RtcService` (`v2::Rtc`) which provide remote control over the emulator (VM ops, screenshots, input injection, and zero-copy WebRTC UI streaming).
* **In-Process WebRTC Videobridge:** Hosts WebRTC signaling directly inside the gRPC server using `MediaTrackProvider` (`InProcessVideoSource` / `InProcessAudioSource`) and `InProcessInputSender` (direct event dispatching), eliminating IPC overhead and external process forwarding.
* **Security:**
    *   **TLS:** Configurable via QOM properties (`tls_cer`, `tls_key`, `tls_ca`).
    *   **Auth:** Supports token-based authentication (`-grpc-use-token`) and JWTs.
* **Advertisement:** Writes connection details (port, token, certificate paths) to a discovery file (INI format) in `discovery_dir`, allowing tools like Android Studio to find the running emulator instance.

## Dependencies
* **Core:** `//emulator/plugin/avd` (AVD Info).
* **Service:** `//emulator/grpc/services/emulator_controller`.
* **Videobridge:** `//emulator/videobridge:rtc_service`, `//emulator/videobridge:in_process_media_providers`, `//emulator/videobridge:videobridge_core`.
* **Forwarding:** `//emulator/grpc/services/forwarder` (Service/UI forwarding).

## Threading Model
* **gRPC Thread Pool:** The gRPC server runs on its own thread pool.
* **Main Loop Interaction:** Interactions with QEMU core (VM operations, display) are marshalled to the main loop via `QemuEventLoop` or `GlobalEventLoop`.
