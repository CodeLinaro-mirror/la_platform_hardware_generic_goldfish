# Component: VM Interface Plugin

**Role:** Provides a C++ abstraction (`VmOperations`) for controlling the QEMU Virtual Machine (Start, Stop, Reset, Query).
**Location:** `hardware/generic/goldfish/emulator/plugin/vminterface`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `VmOperations` | `:vminterface` | `.../vm_interface.h` | Abstract interface for VM control. `RunStateEvents()` publishes run state changes. |
| `CreateQemuVmOperations(loop)` | `:vm_ops` | `.../qemu_vm_operations.h` | Creates the QEMU-backed `VmOperations`, delivering run state changes on `loop`. |
| `ScopedVmLock` | `:vm_lock` | `src/vm_lock.h` | RAII wrapper for the QEMU Big Lock (BQL). |

## Critical Infrastructure
* **QemuVmOperations:** Implementation of `VmOperations` that calls internal QEMU C functions (`vm_start`, `vm_stop`, `qemu_system_reset_request`). It registers a QEMU run-state handler for its lifetime. `AvdExtendedUniverse` owns the only instance, bound to the global event loop; consumers receive it through `AvdUniverse::GetVmOperations()`.
* **BQL Management:** Most QEMU state is protected by the Big QEMU Lock (BQL). `ScopedVmLock` ensures this lock is held when calling into QEMU internals from external threads (like gRPC or worker threads).

## Dependencies
* **Upstream:** `@qemu` (Sysemu, Runstate, Reset), `//emulator/libs/async:async_api`.

## Threading Model
* **Thread Safe:** The `VmOperations` methods acquire `ScopedVmLock` (BQL) internally and may be called from any thread except the loop passed at construction.
* **Run state events:** QEMU reports run state changes on the thread that made them, with the BQL held. `RunStateEvents()` posts them to the loop passed at construction, so callbacks never run under the BQL. Events still queued when a callback is removed, or when `VmOperations` is destroyed, are dropped.
* **Off-loop invariant:** Neither QEMU nor callers may change the run state on that loop's thread, because the dispatcher would then run the callbacks inline under the BQL. `QemuVmOperations` `DCHECK`s this.
* **Subscribers must not block** on the BQL or the QEMU loop. BQL holders wait for the global loop (for example `AvdExtendedUniverse::WaitUntilEventLoopsIdle`), so blocking there deadlocks.
