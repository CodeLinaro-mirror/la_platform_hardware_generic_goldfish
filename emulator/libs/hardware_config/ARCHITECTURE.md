# Component: Emulator Configuration

**Role:** Manages emulator hardware configurations, environment discovery (SDK/AVD paths), and device type definitions.
**Location:** `hardware/generic/goldfish/emulator/config`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `HardwareConfig` | `:hardware_config` | `.../hardware_config.h` | Represents the full hardware specification of an AVD. |
| `ConfigDirs` | `:config_dirs` | `.../config_dirs.h` | Helpers to locate SDK, AVD, and Discovery directories. |
| `DeviceType` | `:device_type` | `.../device_type.h` | Enum for device categories (Phone, TV, Wear, etc.). |

## Critical Infrastructure
* **Hardware Config Generation:** Uses a Python tool (`tools/gen-hw-config.py`) to process `data/hardware-properties.ini` and generate a C++ header (`include/avd/hw-config-defs.h`) containing macro-based field definitions.
* **Path Discovery:** `ConfigDirs` implements the complex logic for finding the Android SDK and user AVDs across Linux, macOS, and Windows, respecting multiple environment variables (`ANDROID_SDK_ROOT`, `ANDROID_AVD_HOME`, etc.).
* **Persistence:** `HardwareConfig::Load/Write` handles the transformation between the standard `config.ini` format and the emulator's runtime state.

## Dependencies
* **Filesystem:** `//emulator/libs/files:ini_file`.
* **System:** `//emulator/libs/system` (for OS-specific path logic).
* **Base:** `@aemu//base:aemu-base`.

## Threading Model
* **Thread Safe:** `ConfigDirs` methods are generally stateless and safe to call. `HardwareConfig` is a data container and requires external synchronization if shared across threads.
