# Component: Emulator SDK

**Role:** Provides a localized Android SDK environment for building and testing the emulator.
**Location:** `hardware/generic/goldfish/emulator/sdk`
**Namespace:** N/A (Data/Assets)

## Integration Guide
| Target | Description |
| :--- | :--- |
| `//emulator/sdk:sdk-marker-files` | Empty marker files used to satisfy `ConfigDirs::IsValidSdkRoot` checks during tests. |

## Critical Infrastructure
* **Directory Structure:** Mimics a real Android SDK layout:
    *   `platform-tools/`: Contains ADB and fastboot.
    *   `platforms/`: Contains Android platform definitions (e.g., `android-34`).
    *   `system_images/`: Contains AVD system images (kernel, ramdisk, system.img).
    *   `build-tools/`: Contains AAPT, DX, etc.
* **Testing:** Used by Bazel tests (`launch_emulator`, `avd_test`) to provide a valid `ANDROID_SDK_ROOT` without relying on the host machine's environment.

## Dependencies
* **External:** Populated by prebuilts or empty markers during the build process.
