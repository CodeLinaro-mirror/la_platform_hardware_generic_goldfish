# Component: Crash Reporting & Hang Detection

**Role:** Manages out-of-process crash reporting (via Crashpad) and monitors for emulator hangs.
**Location:** `hardware/generic/goldfish/emulator/crashreport`
**Namespace:** `android::crashreport`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `crashhandler_init` | `:init` | `.../crash_initializer.h` | C-entry point to initialize the system at startup. |
| `CrashReporter` | `:crash_reporter` | `.../crash_reporter.h` | Singleton for adding annotations or triggering a crash. |
| `HangDetector` | `:hang_detector` | `.../hang_detector.h` | Service to monitor event loops for hangs. |
| `upload_crashes` | `:init` | `.../crash_system.h` | Starts the background upload of pending reports. |

## Critical Infrastructure
* **Crashpad Integration:** Starts an out-of-process `crashpad_handler` to monitor the emulator. If the emulator crashes, the handler saves a minidump to a local database.
* **Annotations:** `CrashReporter::attachData` allows attaching key-value pairs (e.g., version, GPU driver) to the crash dump.
* **Hang Detection:**
    *   **Looper Watcher:** Registers `goldfish::async::EventLoop` instances. It posts heartbeat tasks to the loop; if they don't complete within a timeout, it triggers a "hang" crash.
    *   **Predicates:** Allows registering custom lambda functions that return true if a specific subsystem is stuck.
* **Consent:** Logic to handle user consent before uploading dumps to Google's crash servers.

## Dependencies
* **Upstream:** `@crashpad` (Core engine), `@abseil-cpp`.
* **Internal:** `//android/async` (For loop monitoring), `//android/system`.

## Threading Model
* **CrashSystem:** Initialization happens on the main thread. Uploads are spawned in detached threads.
* **HangDetector:** Runs a dedicated worker thread that periodically polls watched loopers and evaluates predicates.
* **CrashReporter:** Annotations are thread-safe (guarded by Crashpad's internal mechanics).

## Flows & Guides
* [Hang Detection Lifecycle](docs/hang_detection_flow.md)
