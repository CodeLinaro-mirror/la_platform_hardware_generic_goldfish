# Component: QEMU Bottom Half (BH)

**Role:** C++ RAII wrapper for QEMU's Bottom Half (BH) mechanism.
**Location:** `hardware/generic/goldfish/emulator/libs/QEMUBH`
**Namespace:** `goldfish::qemu`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `QEMUBHPtr` | `@goldfish//emulator/libs/QEMUBH` | `include/goldfish/qemu/qemubh.h` | Smart pointer (`std::unique_ptr`) for `QEMUBH`. |
| `MakeQemuBh` | `@goldfish//emulator/libs/QEMUBH` | `include/goldfish/qemu/qemubh.h` | Factory to create a BH from a C function or C++ lambda. |

## Critical Infrastructure
* **Resource Management:** Automates `qemu_bh_delete` via a custom deleter.
* **Lambda Support:** Wraps C++ `std::function` (and lambdas with captures) into a C-compatible void* trampoline, managing the lifetime of the functor alongside the BH.

## Dependencies
* **External:** `@qemu` (Provides `qemu_bh_new`, `qemu_bh_delete`).

## Threading Model
* **QEMU Context:** These BHs are executed by the QEMU main loop (or AioContext). They are *not* arbitrary threads; they run serialized with other QEMU events.
