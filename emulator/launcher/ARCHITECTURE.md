# Component: Emulator Launcher

**Role:** The main entry point for the emulator. Coordinates the startup of QEMU, `netsimd`, and the crash reporting system.
**Location:** `hardware/generic/goldfish/emulator/launcher`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Launcher` | `:launcher` | `.../launcher.cc` | Main coordination class. |
| `Emulator` | `:emulator` | `.../emulator.h` | Configures and launches the QEMU process. |
| `Avd` | `:avd` | `.../avd.h` | Represents the Android Virtual Device configuration. |

## Critical Infrastructure
* **Startup Sequence:**
    1.  Parses command line arguments (`emulator/cmdline`).
    2.  Resolves paths (SDK, AVD, Binaries).
    3.  Initializes crash reporting.
    4.  Locates/Launches `netsimd` (network simulation daemon) if needed.
    5.  Configures QEMU arguments (`Emulator::getCmdline`) based on the AVD and options.
    6.  Launches the QEMU process.
* **Process Management:** Uses `goldfish::async::UvProcessLauncher` (libuv) to spawn and monitor child processes (`qemu`, `netsimd`).
* **Port Management:** Finds a free TCP port (e.g., 5554) to establish the **Emulator Serial Number** (Identity).
    *   **Serial Port (e.g., 5554):** Reserved by the launcher with a dummy server to prevent ID conflicts. It is **not** a functional Telnet console.
    *   **ADB Port (e.g., 5555):** Assigned to the internal ADB bridge.
    *   **gRPC Port:** Derived from the serial port (Serial + 3000, e.g., 8554) or configured explicitly.

## Dependencies
* **Core:** `//emulator/cmdline`, `//emulator/config`.
* **Async:** `//android/async` (Event loop, process launching).
* **Crash:** `//emulator/crashreport`.
* **Plugins:** Links against all emulator plugins to ensure they are available to QEMU.

## Threading Model
* **Main Loop:** Runs on a `LibuvEventLoop`. Most coordination logic (process monitoring, port finding) happens here.
* **Signals:** Handles signals (SIGINT, etc.) and forwards them to the child emulator process to allow graceful shutdown.

## Flows & Guides
* [Startup Lifecycle](docs/startup_flow.md)
