# Component: Emulator Configuration

**Role:** Manages emulator hardware configurations, memory configurations, and device type definitions.
**Location:** `hardware/generic/goldfish/emulator/libs/hardware_config`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `HardwareConfig` | `@goldfish//emulator/libs/hardware_config:hardware_config` | `include/android/goldfish/hardware_config.h`, `hardware_config.cc` | Represents the full hardware specification of an AVD (`config.ini` / `hardware-properties.ini`). |
| `FakeHardwareConfig` | `@goldfish//emulator/libs/hardware_config:fake_hardware_config` | `test/android/goldfish/fake_hardware_config.h` | Test double for `HardwareConfig` used in launcher and drive configuration tests. |
| `MemoryConfig` | `@goldfish//emulator/libs/hardware_config:hardware_config` | `include/android/goldfish/memory_config.h`, `memory_config.cc` | Validates and calculates RAM and memory limits (`@goldfish//emulator/libs/hardware_config:memory_config_test`). |
| `DeviceType` | `@goldfish//emulator/libs/hardware_config:device_type` | `include/android/goldfish/device_type.h` | Enum for device categories (Phone, TV, Wear, etc.). |
| `ConfigDirs` | `//emulator/launcher:config_dirs` | `include/android/goldfish/config_dirs.h` | Helpers to locate SDK, AVD, and Discovery directories. |

## Critical Infrastructure
* **Hardware Config Generation (`data/hardware-properties.ini`):** Uses `tools/gen-hw-config.py` (`@goldfish//emulator/libs/hardware_config:gen-hw-config`) to process `emulator/libs/hardware_config/data/hardware-properties.ini` and generate `include/avd/hw-config-defs.h`.
* **Cross-Package CLI Flag & Drive Consumption:** Hardware properties defined in `data/hardware-properties.ini` (such as `disk.cachePartition` / `-cache` / `-cache_size`) are paired with CLI option definitions in `emulator/launcher/cmdline/include/android/cmdline_options.h` (`cmdline_option.cc`, `help.cc`), resolved in `emulator/launcher/avd.cc` (`include/android/goldfish/image_list.h`), and consumed during QEMU drive setup in `emulator/launcher/launch_qemu/configure_drives.cc` (`ConfigureDrives`, tested in `configure_drives_test.cc`).
* **Path Discovery:** `ConfigDirs` implements the logic for finding the Android SDK and user AVDs across Linux, macOS, and Windows, respecting environment variables (`ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`, etc.).
* **Persistence:** `HardwareConfig` handles parsing and transformation of the standard `config.ini` format into the emulator's runtime state.

## Dependencies
* **Filesystem:** `//emulator/libs/ini_file`.
* **System:** `//emulator/libs/system` (for OS-specific path logic).
* **Host Common:** `//emulator/libs/host-common`.

## Threading Model
* **Thread Safe:** `DeviceType` and `ConfigDirs` methods are stateless and safe to call. `HardwareConfig` is a data container and requires external synchronization if shared across threads.
