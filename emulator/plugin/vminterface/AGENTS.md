# VM Interface (`//emulator/plugin/vminterface/...`) — Agent Playbook

**Namespace:** `android::goldfish`

Host-side control boundary for QEMU run state and snapshots (`VmOperations`, `QemuVmOperations`, `ScopedVmLock`), used by gRPC services (`HypervisorServiceImpl`, `SystemServiceImpl`) and HALs.

---

## 1. Key Targets & Files

| Component | Bazel Target | Files | Role |
| :--- | :--- | :--- | :--- |
| `VmOperations` | `//emulator/plugin/vminterface:vminterface` | [vm_interface.h](include/android/goldfish/vm_interface.h) | Abstract interface: `Start`, `Stop`, `Pause`, `Resume`, `Reset`, `Shutdown`, `SaveSnapshot`, `LoadSnapshot`, `LoadSnapshotResume`, `DeleteSnapshot`, `ListSnapshots`, `RunStateEvents()`, `SnapshotEvents()`, and the pure virtual queries `IsRunning()`, `GetConfiguration()`, `GetRunState()`, `SystemShutdownRequest()`. `GetSnapshotProgress()` is non-pure, so fakes need not override it. |
| `CreateQemuVmOperations` | `//emulator/plugin/vminterface:vm_ops` | [qemu_vm_operations.h](include/android/goldfish/qemu_vm_operations.h), [qemu_vm_operations.cc](qemu_vm_operations.cc) | Production implementation: mutations run on `qemu_loop` through `RunOnQemuLoop` (→ `PostAndWait`); events are published on the event loop passed to the constructor (`goldfish::avd::globalEventLoop()` in production). |
| `ScopedVmLock`, `RecursiveScopedVmLock` | `//emulator/plugin/vminterface:vm_lock` | [vm_lock.h](vm_lock.h), [vm_lock.cc](vm_lock.cc) | RAII around the BQL (`bql_lock` / `bql_unlock`). `vm_lock.h` is a `srcs` header: include it as `"emulator/plugin/vminterface/vm_lock.h"`. |
| Test doubles | `//emulator/plugin/vminterface:test_vminterface`, `//emulator/plugin/vminterface:vminterface-test` | [fake_vm_operations.h](test/fake_vm_operations.h), [vm_mock.h](test/vm_mock.h), [vm_test.cc](vm_test.cc) | `FakeVmOperations` for services (`#include "fake_vm_operations.h"`); `vm_test.cc` (`//emulator/plugin/vminterface:vminterface_unittests`) tests `QemuVmOperations`. |

---

## 2. Threading & BQL Rules

1. **Mutating methods only from other threads.** `Stop`, `Start`, `Pause`, `Resume`, `Reset`, `Shutdown`, `SaveSnapshot`, `LoadSnapshot`, `LoadSnapshotResume`, `DeleteSnapshot`, and `ListSnapshots` block in `RunOnQemuLoop` → `qemu_loop_->PostAndWait(...)`, whose task runs in a QEMU bottom half holding the BQL. From the event loop they hit `LOG(FATAL) "VmOperations cannot be called from the event loop."`; from `qemu_loop` they hit `LOG(FATAL) "postAndWait cannot be called from the event loop."` (in `EventLoop::PostAndWait`). Both are fatal in every build mode. Never call them while holding the BQL (vCPU or device callbacks, inside a `ScopedVmLock`): the bottom half can't run, so `PostAndWait` deadlocks without any `FATAL`.
2. **Safe from any thread:** `IsRunning()`, `GetRunState()`, `GetConfiguration()`, `GetSnapshotProgress()` (reads under the leaf `progress_mutex_`), and `SystemShutdownRequest()`. To shut down from a HAL or event-loop callback, call `SystemShutdownRequest(QemuShutdownCause::GuestShutdown)`, never `Shutdown()`.
3. **Event callbacks** from `RunStateEvents()` / `SnapshotEvents()` run on the event loop given to the constructor, never on the thread that changed state. Inside them, don't take the BQL, don't call mutating `VmOperations` methods, and never remove or destroy your own subscription (`RemoveCallback` waits for in-flight callbacks). At teardown the QEMU thread waits for these loops to go idle (`AvdExtendedUniverse::WaitUntilEventLoopsIdle`), so blocking on the BQL there deadlocks shutdown.
4. **Nested BQL:** use `RecursiveScopedVmLock` when the caller may already hold the BQL. `ScopedVmLock`'s destructor releases the BQL even if an outer scope acquired it.
5. **No `SnapshotStore` I/O inside `RunOnQemuLoop`.** Name validation (`ValidateSnapshotName`), the `-no-snapshot` check, and `SnapshotStore` `Exists` / `Prepare` / `ReadVersion` / `IsCompatible` / `Remove` run on the calling thread before or after `RunOnQemuLoop`. The one sanctioned exception is `RecordCompletedSave()`, which writes `<avd>/snapshots/<name>/version.txt` from QEMU's snapshot-progress handler (`OnSnapshotProgress`, QEMU main thread, BQL held) when a save completes; this is not a `SnapshotEvents()` callback.

---

## 3. Testing

- **Services:** inject `FakeVmOperations vm_operations_{loop_.get()};` with a `TestEventLoop`, as `hypervisor_service_impl_test.cc` does (`system_service_impl_test.cc` uses a local gMock `MockVmOps` instead).
- **`QemuVmOperations`:** extend `QemuVmOperationsTest` / `QemuVmOperationsDeathTest` in [vm_test.cc](vm_test.cc). Construct a `::goldfish::avd::FakeAvdUniverse` for save/load (they call `::goldfish::avd::GetAvd()`), deliver events with `loop_->RunAll()`, and check that QEMU calls ran on `qemu_loop` with `mock_snapshot_last_call_thread()` ([vm_mock.h](test/vm_mock.h)).

```bash
bazel test --test_output=errors @goldfish//emulator/plugin/vminterface:vminterface_unittests
bazel test --config=tsan --test_output=errors @goldfish//emulator/plugin/vminterface:vminterface_unittests
```
