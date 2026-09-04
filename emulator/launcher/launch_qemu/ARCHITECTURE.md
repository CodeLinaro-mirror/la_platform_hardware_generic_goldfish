# Component: Launch QEMU

**Role:** Translates high-level AVD configurations, hardware properties, and command-line options into low-level QEMU process arguments, virtual device parameters, and storage topologies.
**Location:** `hardware/generic/goldfish/emulator/launcher/launch_qemu`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `LaunchQemu` | `:launch_qemu` | `launch_qemu.h` | Main argument builder that compiles the full QEMU CLI execution command. |
| `Device` | `:device` | `device.h` | Base interface for virtual device argument generators. |
| `EmulatorConfig` | `:emulator_config` | `emulator_config.h` | Consolidated view of command-line flags, AVD settings, and paths. |

## Critical Infrastructure
* **Device Argument Generators:**
    *   **Display & GPU:** Configures `-device virtio-vga` or `-device goldfish_display`, angle/swiftshader host rendering backend.
    *   **Storage & Drives:** Configures block devices, overlay snapshots, and ramdisks (`configure_drives.h`, `user_data_drive.h`).
    *   **Networking & WiFi:** Sets up tap/slirp network interfaces, `netsimd` bridges, and vsock channels.
    *   **gRPC & IPC:** Configures QOM properties for the in-process gRPC server and WebRTC videobridge.
* **Hermetic ROM Validation:** Verifies that required QEMU ROMs and firmware blobs exist prior to process execution.

## Dependencies
* **AVD & Cmdline:** `//emulator/launcher:avd`, `//emulator/launcher:input_paths`, `//emulator/launcher/cmdline`.
* **Async Config:** `//emulator/libs/async:launch_config`.
* **Abseil:** `@abseil-cpp//absl/status:statusor`, `@abseil-cpp//absl/strings`.

## Threading Model
* **Thread Safe:** Stateless argument generation; safe for invocation during launcher startup.
