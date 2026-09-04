# Component: File System Watcher

**Role:** Cross-platform filesystem monitoring and notification service.
**Location:** `hardware/generic/goldfish/emulator/libs/file_system_watcher`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `FileSystemWatcher` | `:file_system_watcher` | `include/android/base/file_system_watcher.h` | Cross-platform directory monitoring (inotify, FSEvents, ReadDirectoryChangesW). |

## Critical Infrastructure
* **FileSystemWatcher:**
    *   **Platform Specifics:** Implements efficient OS-specific watching mechanisms (Linux inotify, macOS FSEvents/kqueue, Windows ReadDirectoryChangesW) to avoid polling.
    *   **Threading:** Starts a dedicated thread per watcher instance to handle blocking OS calls.
    *   **Event Handling:** Dispatches path creation, modification, deletion, and rename notifications to registered callbacks.

## Dependencies
* **Base:** `//emulator/libs/base`.
* **File:** `//emulator/libs/file`.
* **Abseil:** `@abseil-cpp//absl/synchronization`, `@abseil-cpp//absl/log`.

## Threading Model
* **FileSystemWatcher:** Thread-safe start/stop. The callback is invoked on the watcher's dedicated worker thread. `Start()` blocks until the OS-specific watcher is fully initialized and monitoring events.
