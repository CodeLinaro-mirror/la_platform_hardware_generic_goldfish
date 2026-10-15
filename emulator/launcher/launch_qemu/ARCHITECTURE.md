# Component: Launch QEMU

**Role:** Translates high-level AVD configurations, hardware properties, and command-line options into low-level QEMU process arguments, virtual device parameters, and storage topologies.
**Location:** `hardware/generic/goldfish/emulator/launcher/launch_qemu`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Function | Bazel Target | Header / Source Files | Description |
| :--- | :--- | :--- | :--- |
| `LaunchQemu` | `@goldfish//emulator/launcher/launch_qemu:launch_qemu` | `launch_qemu.h`, `launch_qemu.cc` | Main argument and environment builder (`LaunchQemu::addDevice`, `LaunchQemu::launch_config`) that compiles the full QEMU CLI execution command and process environment. |
| `Device` | `@goldfish//emulator/launcher/launch_qemu:device` | `device.h` | Base interface (`initialize`, `getQemuParameters`, `getQemuEnv`, `Device::EscapeQemuOptionValue`) for virtual device argument and environment generators registered via `LaunchQemu::addDevice`. Tested in `device_test.cc` (`@goldfish//emulator/launcher/launch_qemu:device_test`). |
| `EnvPassthroughDevice` | `@goldfish//emulator/launcher/launch_qemu:env_passthrough_device` | `env_passthrough_device.h`, `env_passthrough_device.cc` | Passes allowlisted host environment variables through to the QEMU process environment. Tested in `env_passthrough_device_test.cc` (`@goldfish//emulator/launcher/launch_qemu:env_passthrough_device_test`). |
| `PluginInit` | `@goldfish//emulator/launcher/launch_qemu:plugin_init` | `plugin_init.h`, `plugin_init.cc` | Configures environment variables for early goldfish plugin loading and initialization (`aemu_main.c` and `plugin_init`). Tested in `plugin_init_test.cc` (`@goldfish//emulator/launcher/launch_qemu:plugin_init_test`). |
| `ConfigureDrives` | `@goldfish//emulator/launcher/launch_qemu:configure_drives` | `configure_drives.h`, `configure_drives.cc` | Configures system, vendor, ramdisk, encryptionkey, and userdata drives for `LaunchQemu` without redundant `.img` $\rightarrow$ `-qemu.img` file copying. Tested in `configure_drives_test.cc` (`@goldfish//emulator/launcher/launch_qemu:configure_drives_test`). |
| `UserDataDrive` | `@goldfish//emulator/launcher/launch_qemu:user_data_drive` | `user_data_drive.h`, `user_data_drive.cc` | Configures the writable `userdata-qemu.img` / `userdata.img` disk drive and wipe-data preparation. Tested in `user_data_drive_test.cc` (`@goldfish//emulator/launcher/launch_qemu:user_data_drive_test`). |
| `DiskDrive` | `@goldfish//emulator/launcher/launch_qemu:disk_drive` | `disk_drive.h`, `disk_drive.cc` | `Device` implementation for individual virtio-blk / qcow2 / raw disk images. Tested in `disk_drive_test.cc` (`@goldfish//emulator/launcher/launch_qemu:disk_drive_test`). |
| `EmulatorConfig` | `@goldfish//emulator/launcher/launch_qemu:emulator_config` | `emulator_config.h` | Consolidated view of command-line flags (`AndroidOptions`), `Avd` (`//emulator/launcher:avd`, `image_list.h`), and `HardwareConfig`. |

## Critical Infrastructure
* **Device Argument Generators (`LaunchQemu::addDevice`):**
    *   **Display & GPU:** Configures `-device virtio-vga` or `-device goldfish_display`, angle/swiftshader host rendering backend.
    *   **Storage & Drives (`ConfigureDrives`, `UserDataDrive`, `DiskDrive`):** Configures block devices, overlay snapshots, and ramdisks (`configure_drives.h`, `configure_drives.cc`, `user_data_drive.h`, `user_data_drive.cc`, `disk_drive.h`, `disk_drive.cc`) backed by `Avd` image paths (`emulator/launcher/avd.cc`, `emulator/launcher/include/android/goldfish/image_list.h`).
    *   **Networking & WiFi:** Sets up tap/slirp network interfaces, `netsimd` bridges, and vsock channels.
    *   **gRPC & IPC:** Configures QOM properties for the in-process gRPC server and WebRTC videobridge.
* **Hermetic ROM Validation:** Verifies that required QEMU ROMs and firmware blobs exist prior to process execution.

## Dependencies
* **AVD & Cmdline:** `//emulator/launcher:avd`, `//emulator/launcher:input_paths`, `//emulator/launcher/cmdline`.
* **Async Config:** `//emulator/libs/async:launch_config`.
* **Abseil:** `@abseil-cpp//absl/status:statusor`, `@abseil-cpp//absl/strings`.

## Threading Model
* **Thread Safe:** Stateless argument generation; safe for invocation during launcher startup.
