# Component: System Plugin

**Role:** Provides system-level implementations backed by QEMU internals.
**Location:** `hardware/generic/goldfish/emulator/plugin/system`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `QemuClock` | `:qemu_clock` | `.../qemu_clock.h` | Implementation of `IClock` using QEMU timers. |

## Critical Infrastructure
* **QemuClock:** Maps `ClockType` (Virtual, Host, Realtime) to QEMU's `QEMU_CLOCK_*` constants and calls `qemu_clock_get_ns`. This ensures that the emulator's C++ code (e.g., event loops, schedulers) respects the VM's time scaling and pauses.

## Dependencies
* **Upstream:** `@qemu` (`qemu/timer.h`).
* **Internal:** `//emulator/libs/system:clock` (Interface definition).

## Threading Model
* **Thread Safe:** `qemu_clock_get_ns` is generally thread-safe in QEMU.
