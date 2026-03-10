# Component: Android Files

**Role:** Provides file system utilities for configuration management and monitoring.
**Location:** `hardware/generic/goldfish/android/files`
**Namespace:** `android::base` (Watcher), `android::goldfish` (IniFile)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `FileSystemWatcher` | `:file_system_watcher` | `.../file_system_watcher.h` | Cross-platform directory monitoring (inotify, FSEvents, ReadDirectoryChangesW). |
| `IniFile` | `:ini_file` | `.../ini_file.h` | Parsing and serialization of `.ini` configuration files. |

## Critical Infrastructure
* **IniFile:**
    *   **Ordered Map:** Preserves the order of keys from the backing file (important for human readability).
    *   **Typed Access:** Provides getters/setters for `bool`, `int`, `double`, `DiskSize` (e.g., "512M").
    *   **Persistence:** Can write changes back to disk, optionally discarding empty values.
* **FileSystemWatcher:**
    *   **Platform Specifics:** Implements efficient OS-specific watching mechanisms to avoid polling.
    *   **Threading:** Starts a dedicated thread per watcher instance to handle blocking OS calls.

## Dependencies
* **System:** `//emulator/libs/system` (OS calls).
* **Process:** `//emulator/libs/process` (Thread management for watcher).

## Threading Model
* **IniFile:** Not thread-safe.
* **FileSystemWatcher:** Thread-safe start/stop. The callback is invoked on the watcher's dedicated thread. `Start()` blocks until the OS-specific watcher is fully initialized and monitoring events.
