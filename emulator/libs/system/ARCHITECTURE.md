# Component: Android System

**Role:** Abstraction layer for Operating System services.
**Location:** `hardware/generic/goldfish/emulator/libs/system`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `System` | `:system` | `include/android/base/system.h` | Abstract interface for Env vars, Time, Paths, OS Info. |
| `Clock` | `:clock` | `include/android/base/clock.h` | Abstract interface for time retrieval (`virtual`, `realtime`, `host`). |
| `FdUtil` | `:fd_util` | `include/android/base/fd_util.h` | File descriptor utilities and non-blocking helpers. |

## Critical Infrastructure
* **System Singleton:** `System::Get()` provides access to the global OS interface. This can be swapped for a mock (`TestSystem`) during unit tests to simulate environment variables, time, or file system states.
* **Storage Capacity:** `StorageCapacity` class provides type-safe handling of byte sizes (MiB, GiB) with user-defined literals (e.g., `512_MiB`).
* **Platform Independence:** Hides differences between Windows (`_wgetenv`, `Sleep`) and POSIX (`getenv`, `usleep`).

## Dependencies
* **Core:** `@abseil-cpp//absl/log`, `@abseil-cpp//absl/time`, `@abseil-cpp//absl/status:statusor`.
* **Libraries:** `//emulator/libs/file`, `//emulator/libs/process`.

## Threading Model
* **Thread Safe:** `System` methods are generally thread-safe.
