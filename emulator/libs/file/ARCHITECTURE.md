# Component: File System Primitives

**Role:** Cross-platform filesystem operations, atomic file writes, path manipulation, and disk capacity units.
**Location:** `hardware/generic/goldfish/emulator/libs/file`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `File` | `:file` | `include/goldfish/file/file.h` | Helpers for file and path operations (`mkdir`, `rm`, `exists`, `touch`). |
| `AtomicFile` | `:file` | `include/goldfish/file/file_atomic.h` | Atomic write replacement using temporary files. |
| `StorageCapacity` | `:file` | `include/goldfish/file/storage_capacity.h` | Type-safe handling of byte sizes (MiB, GiB, TiB) with literals (e.g. `512_MiB`). |

## Critical Infrastructure
* **Cross-Platform Path Handling:** Normalizes directory separators, path concatenation, and Windows UNC prefixes.
* **Atomic File Writes:** Guarantees that written files are flushed to disk before atomically replacing target files.
* **Storage Capacity Types:** Strongly-typed representation of byte capacities with arithmetic and string formatting.

## Dependencies
* **Base:** `//emulator/libs/base`, `//emulator/libs/status_macros`, `//emulator/libs/system:eintr_wrapper`.
* **Abseil:** `@abseil-cpp//absl/status:statusor`, `@abseil-cpp//absl/strings`, `@abseil-cpp//absl/log`.

## Threading Model
* **Thread Safe:** File helper functions are stateless and thread-safe.
