# Component: VM Interface Plugin

**Role:** Provides a C++ abstraction (`VmOperations`) for controlling the QEMU Virtual Machine (Start, Stop, Reset, Query).
**Location:** `hardware/generic/goldfish/emulator/plugin/vminterface`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `VmOperations` | `:vminterface` | `.../vm_interface.h` | Abstract interface for VM control. `RunStateEvents()` publishes run state changes. |
| `CreateQemuVmOperations(event_loop, qemu_loop)` | `:vm_ops` | `.../qemu_vm_operations.h` | Creates the QEMU-backed `VmOperations`, delivering run state changes on `event_loop` and executing VM operations on `qemu_loop`. |
| `ScopedVmLock` | `:vm_lock` | `src/vm_lock.h` | RAII wrapper for the QEMU Big Lock (BQL). |

## Critical Infrastructure
* **QemuVmOperations:** Implementation of `VmOperations` that calls internal QEMU C functions (`vm_start`, `vm_stop`, `qemu_system_reset_request`, snapshot operations) on `qemu_loop`. It registers a QEMU run-state handler for its lifetime. `AvdExtendedUniverse` owns the only instance, bound to the global event loop and `QemuEventLoop`; consumers receive it through `AvdUniverse::GetVmOperations()`.
* **BQL Management:** Most QEMU state is protected by the Big QEMU Lock (BQL). `QemuVmOperations` dispatches mutating and snapshot calls onto `qemu_loop` via `PostAndWait` (which runs inside a QEMU Bottom Half with the BQL already held), and uses `RecursiveScopedVmLock` when registering/unregistering its run-state handler at construction and destruction.

## Dependencies
* **Upstream:** `@qemu` (Sysemu, Runstate, Reset), `//emulator/libs/async:async_api`.

## Threading Model
* **Dispatched on `qemu_loop`:** Mutating and snapshot `VmOperations` methods (`stop`, `start`, `pause`, `resume`, `reset`, `Shutdown`, `SaveSnapshot`, `LoadSnapshot`, `LoadSnapshotResume`, `DeleteSnapshot`, `ListSnapshots`) dispatch synchronously onto `qemu_loop` via `PostAndWait`. They are expected to be called from external threads (such as gRPC server or worker threads) and fatally abort if called from either `event_loop` or `qemu_loop`. `SaveSnapshot`, `LoadSnapshot`, and `DeleteSnapshot` check `getRunState()` on `qemu_loop` and reject calls with `FAILED_PRECONDITION` while a snapshot or migration is in progress (`SaveVm`, `RestoreVm`, etc.) or while the VM is shutting down.
* **Read-only and atomic calls:** `isRunning()`, `getRunState()`, `getConfiguration()`, and `systemShutdownRequest()` execute directly without posting to `qemu_loop` and may be called from any thread (including `qemu_loop`, `RunStateEvents()` callbacks, and hang detector predicates).
* **Run state events:** QEMU reports run state changes on the thread that made them (`qemu_loop`), with the BQL held. `RunStateEvents()` posts them to `event_loop`, so callbacks never run under the BQL. Events still queued when a callback is removed, or when `VmOperations` is destroyed, are dropped.
* **Off-loop invariant:** Neither QEMU nor callers may change the run state or invoke mutating `VmOperations` methods on `event_loop`, preventing inline callback execution under the BQL, re-entrant callbacks, and cross-loop deadlocks.
* **Subscribers must not block** on the BQL or the QEMU loop, and must not invoke mutating `VmOperations` methods. BQL holders wait for the global loop (for example `AvdExtendedUniverse::WaitUntilEventLoopsIdle`), so blocking there deadlocks.
