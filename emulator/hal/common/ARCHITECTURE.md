# Component: HAL Common

**Role:** Shared definitions and interfaces used across multiple HAL implementations.
**Location:** `hardware/generic/goldfish/emulator/hal/common`
**Namespace:** `goldfish::devices`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `EmulatorResetCallbacks` | `:emulator_reset` | `.../emulator_reset.h` | Interface for registering/unregistering callbacks for the emulator reset event. |

## Critical Infrastructure
* **Decoupling:** Allows HALs to respond to QEMU system resets (e.g., guest reboot) without linking directly against QEMU headers.

## Dependencies
* None. Pure C++ interface.

## Threading Model
* **Context:** Callbacks are typically invoked from the QEMU main loop (or the thread triggering the reset).
