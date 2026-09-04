# Component: Emulator Configuration

**Role:** Manages emulator hardware configurations, memory configurations, and device type definitions.
**Location:** `hardware/generic/goldfish/emulator/libs/hardware_config`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `HardwareConfig` | `:hardware_config` | `include/android/goldfish/hardware_config.h` | Represents the full hardware specification of an AVD. |
| `MemoryConfig` | `:hardware_config` | `include/android/goldfish/memory_config.h` | Validates and calculates RAM and memory limits. |
| `DeviceType` | `:device_type` | `include/android/goldfish/device_type.h` | Enum for device categories (Phone, TV, Wear, etc.). |
| `ConfigDirs` | `//emulator/launcher:config_dirs` | `include/android/goldfish/config_dirs.h` | Helpers to locate SDK, AVD, and Discovery directories. |

## Critical Infrastructure
* **Hardware Config Generation:** Uses a Python tool (`tools/gen-hw-config.py`) to process `data/hardware-properties.ini` and generate a C++ header (`include/avd/hw-config-defs.h`) containing macro-based field definitions.
* **Path Discovery:** `ConfigDirs` implements the logic for finding the Android SDK and user AVDs across Linux, macOS, and Windows, respecting environment variables (`ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`, etc.).
* **Persistence:** `HardwareConfig` handles parsing and transformation of the standard `config.ini` format into the emulator's runtime state.

## Dependencies
* **Filesystem:** `//emulator/libs/ini_file`.
* **System:** `//emulator/libs/system` (for OS-specific path logic).
* **Host Common:** `//emulator/libs/host-common`.

## Threading Model
* **Thread Safe:** `DeviceType` and `ConfigDirs` methods are stateless and safe to call. `HardwareConfig` is a data container and requires external synchronization if shared across threads.
