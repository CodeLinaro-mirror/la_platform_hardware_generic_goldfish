# Component: AVD Info Plugin

**Role:** The "Bootstrap" QEMU device (`avdstart`). It initializes the Goldfish emulator environment, configures hardware, and starts all HAL services.
**Location:** `hardware/generic/goldfish/emulator/plugin/avd`
**Namespace:** `goldfish::avd_info` (C++), `TYPE_AVD` (QOM)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AvdUniverse` | `:info` | `.../avd_info.h` | Container for global AVD state (Properties, Sensors, Clipboard). |
| `getAvd()` | `:info` | `.../avd_info.h` | Singleton accessor for the `AvdUniverse`. |
| `getQemuEventLoop()` | `:impl` | `N/A` (Internal) | Accessor for the main thread `EventLoop` wrapper. |

## Critical Infrastructure
* **Lifecycle Manager:**
    *   **Realize:** Parses `hardware-qemu.ini`, initializes the `AvdUniverse`, creates the `QemuEventLoop`, and registers all HALs (`sensors`, `clipboard`, `camera`, `gps`, etc.) with the connector registry.
    *   **Unrealize:** Shuts down the event loop and cleans up resources.
* **Property Parsing:** Exposes QOM properties (e.g., `avd_name`, `adb_port`) that are set by the emulator launcher command line.
* **Global State:** Holds the canonical "Truth" for the device configuration (`HardwareConfig`) and mutable state (`ObservableValue`s in `AvdUniverse`).

## Dependencies
* **Core:** `//emulator/libs/avd_universe` (State definitions).
* **HALs:** Depends on *every* HAL library to register them.
* **Config:** `//emulator/config:hardware_config`.
* **Async:** `//emulator/libs/async:qemu_event_loop`.

## Threading Model
* **Main Thread:** `realize` runs on the QEMU main thread. It establishes the `QemuEventLoop` which wraps the QEMU AioContext, allowing thread-safe interaction from other threads via `Post`.
