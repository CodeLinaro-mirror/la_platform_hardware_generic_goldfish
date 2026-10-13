# Component: VM Interface Plugin

**Role:** Provides a C++ abstraction (`VmOperations`) for controlling the QEMU Virtual Machine (Start, Stop, Reset, Query).
**Location:** `hardware/generic/goldfish/emulator/plugin/vminterface`
**Namespace:** `android::goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `VmOperations` | `:vminterface` | `.../vm_interface.h` | Abstract interface for VM control. `RunStateEvents()` and `SnapshotEvents()` publish loop-bound events; `GetSnapshotProgress()` provides BQL-free progress reads. |
| `CreateQemuVmOperations(event_loop, qemu_loop)` | `:vm_ops` | `.../qemu_vm_operations.h` | Creates the QEMU-backed `VmOperations`, delivering run state and snapshot events on `event_loop` and executing VM operations on `qemu_loop`. |
| `ScopedVmLock` | `:vm_lock` | `src/vm_lock.h` | RAII wrapper for the QEMU Big Lock (BQL). |

## Critical Infrastructure
* **QemuVmOperations:** Implementation of `VmOperations` that calls internal QEMU C functions (`vm_start`, `vm_stop`, `qemu_system_reset_request`, `save_snapshot`, `load_snapshot`, `delete_snapshot`) on `qemu_loop`. It registers QEMU run-state and snapshot-progress handlers for its lifetime. `AvdExtendedUniverse` owns the only instance, bound to the global event loop and `QemuEventLoop`; consumers receive it through `AvdUniverse::GetVmOperations()`.
* **BQL Management:** Most QEMU state is protected by the Big QEMU Lock (BQL). `QemuVmOperations` dispatches mutating and snapshot calls onto `qemu_loop` via `PostAndWait` (which runs inside a QEMU Bottom Half with the BQL already held), and uses `RecursiveScopedVmLock` when registering/unregistering its run-state and snapshot-progress handlers at construction and destruction.

## Dependencies
* **Upstream:** `@qemu` (Sysemu, Runstate, Reset, Migration/Snapshot), `//emulator/libs/async:async_api`, `//emulator/libs/system:clock`.

## Threading Model
* **Dispatched on `qemu_loop`:** Mutating and snapshot `VmOperations` methods (`stop`, `start`, `pause`, `resume`, `reset`, `Shutdown`, `SaveSnapshot`, `LoadSnapshot`, `LoadSnapshotResume`, `DeleteSnapshot`, `ListSnapshots`) dispatch synchronously onto `qemu_loop` via `PostAndWait`. They are expected to be called from external threads (such as gRPC server or worker threads) and fatally abort if called from either `event_loop` or `qemu_loop`. `SaveSnapshot`, `LoadSnapshot`, and `DeleteSnapshot` check `getRunState()` on `qemu_loop` and reject calls with `FAILED_PRECONDITION` while a snapshot or migration is in progress (`SaveVm`, `RestoreVm`, etc.) or while the VM is shutting down.
* **Read-only and atomic calls:** `isRunning()`, `getRunState()`, `getConfiguration()`, `GetSnapshotProgress()`, and `systemShutdownRequest()` execute directly without posting to `qemu_loop` and may be called from any thread (including `qemu_loop`, `RunStateEvents()` and `SnapshotEvents()` callbacks, and hang detector predicates). `GetSnapshotProgress()` reads the latest `SnapshotProgress` under a leaf `absl::Mutex` (`progress_mutex_`) without acquiring the BQL.
* **Run state and snapshot events:** QEMU reports run state changes and snapshot progress on the thread performing the operation (`qemu_loop`), with the BQL held. `RunStateEvents()` and `SnapshotEvents()` post them to `event_loop` (with `SnapshotPhase::Transferring` updates throttled to 250 ms), so callbacks never run under the BQL. Events still queued when a callback is removed, or when `VmOperations` is destroyed, are dropped.
* **Off-loop invariant:** Neither QEMU nor callers may change the run state, report snapshot progress, or invoke mutating `VmOperations` methods on `event_loop`, preventing inline callback execution under the BQL, re-entrant callbacks, and cross-loop deadlocks.
* **Subscribers must not block** on the BQL or the QEMU loop, and must not invoke mutating `VmOperations` methods. BQL holders wait for the global loop (for example `AvdExtendedUniverse::WaitUntilEventLoopsIdle`), so blocking there deadlocks.
