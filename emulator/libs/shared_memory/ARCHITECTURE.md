# Component: Shared Memory

**Role:** Cross-platform abstraction for creating and mapping shared memory regions.
**Location:** `hardware/generic/goldfish/emulator/libs/shared_memory`
**Namespace:** `goldfish::memory`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `SharedMemory` | `@goldfish//emulator/libs/shared_memory` | `include/goldfish/memory/shared_memory.h` | Represents a named shared memory region. |

## Critical Infrastructure
* **OS Abstraction:**
    *   **POSIX:** Uses `shm_open`, `ftruncate`, `mmap`, `shm_unlink`.
    *   **Windows:** Uses `CreateFileMapping`, `MapViewOfFile`, `UnmapViewOfFile`.
* **Resource Management:** Handles automatic unmapping and handle closing upon destruction.

## Dependencies
* **Core:** `@abseil-cpp//absl/status`.
* **System:** `//emulator/libs/system:win32_utils` (Windows only).

## Threading Model
* **Thread Safe:** Creating objects is thread-safe. Accessing the mapped memory is subject to normal race conditions and requires synchronization if shared between threads/processes.
