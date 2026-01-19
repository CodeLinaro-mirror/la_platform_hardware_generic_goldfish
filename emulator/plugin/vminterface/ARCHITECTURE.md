# Component: VM Interface Plugin

**Role:** Provides a C++ abstraction (`VmOperations`) for controlling the QEMU Virtual Machine (Start, Stop, Reset, Query).
**Location:** `hardware/generic/goldfish/emulator/plugin/vminterface`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `VmOperations` | `:vminterface` | `.../vm_interface.h` | Abstract interface for VM control. |
| `ScopedVmLock` | `:vm_lock` | `src/vm_lock.h` | RAII wrapper for the QEMU Big Lock (BQL). |

## Critical Infrastructure
* **QemuVmOperations:** Implementation of `VmOperations` that calls internal QEMU C functions (`vm_start`, `vm_stop`, `qemu_system_reset_request`).
* **BQL Management:** Most QEMU state is protected by the Big QEMU Lock (BQL). `ScopedVmLock` ensures this lock is held when calling into QEMU internals from external threads (like gRPC or worker threads).

## Dependencies
* **Upstream:** `@qemu` (Sysemu, Runstate, Reset).

## Threading Model
* **Thread Safe:** The `VmOperations` methods are thread-safe because they acquire the `ScopedVmLock` (BQL) internally.
