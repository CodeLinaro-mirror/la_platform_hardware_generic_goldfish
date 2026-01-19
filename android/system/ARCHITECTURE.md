# Component: Android System

**Role:** Abstraction layer for Operating System services.
**Location:** `hardware/generic/goldfish/android/system`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `System` | `:system` | `.../system.h` | Abstract interface for Env vars, Time, Paths, OS Info. |
| `file` | `:file` | `.../file/file.h` | Helpers for file operations (`mkdir`, `rm`, `exists`) wrapping `std::filesystem`. |
| `Clock` | `:clock` | `.../clock.h` | Abstract interface for time retrieval (`virtual`, `realtime`, `host`). |

## Critical Infrastructure
* **System Singleton:** `System::Get()` provides access to the global OS interface. This can be swapped for a mock (`TestSystem`) during unit tests to simulate environment variables, time, or file system states.
* **Storage Capacity:** `StorageCapacity` class provides type-safe handling of byte sizes (MiB, GiB) with user-defined literals (e.g., `512_MiB`).
* **Platform Independence:** Hides differences between Windows (`_wgetenv`, `Sleep`) and POSIX (`getenv`, `usleep`).

## Dependencies
* **Core:** `@abseil-cpp`.
* **Base:** `@aemu//base:aemu-base`.

## Threading Model
* **Thread Safe:** `System` methods are generally thread-safe.
