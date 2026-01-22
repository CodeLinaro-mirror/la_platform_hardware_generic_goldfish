# Component: QEMU File Headers

**Role:** Exposes QEMU's `QEMUFile` types (serialization/migration) to C++ code.
**Location:** `hardware/generic/goldfish/emulator/libs/QEMUFile_h`
**Namespace:** Global (C symbols)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `QEMUFile` | `@goldfish//emulator/libs/QEMUFile_h` | `include/goldfish/qemu_file.h` | QEMU's file abstraction for snapshots/migration. |

## Critical Infrastructure
* **Shim:** This is a thin shim to include `migration/qemu-file-types.h` with correct `extern "C"` linkage.

## Dependencies
* **External:** `@qemu`.
