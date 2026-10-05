# Component: Emulator Launcher

**Role:** The main entry point for the emulator. Coordinates the startup of QEMU, `netsimd`, and the crash reporting system.
**Location:** `hardware/generic/goldfish/emulator/launcher`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Script | Bazel Target | File Path | Description |
| :--- | :--- | :--- | :--- |
| `Launcher` | `@goldfish//emulator/launcher:launcher` | `launcher.h`, `main.cc` | Main C++ coordination and lifecycle supervisor class. |
| `LaunchQemu` | `//emulator/launcher/launch_qemu` | `launch_qemu/launch_qemu.h` | Configures QEMU parameters, drives (`ConfigureDrives`), and device flags. |
| `Avd` & `ImageList` | `@goldfish//emulator/launcher:avd` | `include/android/goldfish/avd.h`, `include/android/goldfish/image_list.h`, `avd.cc` | Resolves Android Virtual Device partition image paths (`.img` vs `-qemu.img`) and hardware properties (`@goldfish//emulator/launcher:avd_test`). |
| `emulator_lib.py` & `launch_emulator.py` | `@goldfish//emulator/launcher:emulator_lib`, `@goldfish//emulator/launcher:launch_emulator` | `tools/emulator_lib.py`, `tools/launch_emulator.py`, `tools/launcher_test.bzl` | Python emulator launcher wrapper (`launch_emulator`, `parse_args`, `wait_for_emulator_discovery_file`) handling companion UI flags such as `--android-device-hub` (hiding the default window, polling the discovery file, and spawning the Device Hub UI binary). Tested in `tools/emulator_lib_test.py` (`@goldfish//emulator/launcher:emulator_lib_test`). |

## Critical Infrastructure
* **Startup Sequence:**
    1.  Parses command line arguments (`//emulator/launcher/cmdline`).
    2.  Resolves paths (SDK, AVD, Binaries).
    3.  Initializes crash reporting (`//emulator/crashreport`).
    4.  Locates/Launches `netsimd` (network simulation daemon) if needed.
    5.  Configures QEMU arguments (`LaunchQemu`) based on the AVD and options.
    6.  Launches the QEMU process.
* **Process Management:** Uses `goldfish::async::UvProcessLauncher` (libuv) to spawn and monitor child processes (`qemu`, `netsimd`).
* **Port Management:** Finds a free TCP port (e.g., 5554) to establish the **Emulator Serial Number** (Identity).
    *   **Serial Port (e.g., 5554):** Reserved by the launcher with a dummy server to prevent ID conflicts. It is **not** a functional Telnet console.
    *   **ADB Port (e.g., 5555):** Assigned to the internal ADB bridge.
    *   **gRPC Port:** Derived from the serial port (Serial + 3000, e.g., 8554) or configured explicitly.

## Dependencies
* **Core:** `//emulator/launcher/cmdline`, `//emulator/libs/hardware_config`.
* **Async:** `//emulator/libs/async` (Event loop, process launching).
* **Crash:** `//emulator/crashreport`.
* **Plugins:** Links against all emulator plugins to ensure they are available to QEMU.

## Threading Model
* **Main Loop:** Runs on a `LibuvEventLoop`. Most coordination logic (process monitoring, port finding) happens here.
* **Signals:** Handles signals (SIGINT, etc.) and forwards them to the child emulator process to allow graceful shutdown.

## Flows & Guides
* [Startup Lifecycle](docs/startup_flow.md)
