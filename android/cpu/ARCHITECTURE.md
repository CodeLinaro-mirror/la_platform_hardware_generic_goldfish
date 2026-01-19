# Component: Android CPU

**Role:** Detects host CPU capabilities and virtualization support (KVM, HAXM, WHPX, HVF).
**Location:** `hardware/generic/goldfish/android/cpu`
**Namespace:** `android`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `CpuAccelerator` | `:cpu` | `.../cpu_accelerator.h` | Enum of supported accelerators. |
| `GetCurrentCpuAccelerator()` | `:cpu` | `.../cpu_accelerator.h` | Function to detect the best available accelerator. |
| `GetCpuInfo()` | `:cpu` | `.../cpu_accelerator.h` | Returns CPU vendor, bitness, and virtualization support flags. |

## Critical Infrastructure
* **Acceleration Detection:** Checks for the presence and usability of:
    *   **KVM:** Linux kernel module (`/dev/kvm`).
    *   **HVF:** Apple Hypervisor.framework (macOS).
    *   **WHPX:** Windows Hypervisor Platform.
    *   **HAXM:** Intel Hardware Accelerated Execution Manager (Legacy).
    *   **AEHD:** Android Emulator Hypervisor Driver (Legacy).
* **CPU Info:** Uses `CPUID` instruction (on x86) or system calls (on ARM/macOS) to determine CPU features.

## Dependencies
* **System:** `//android/system`.
* **Base:** `@aemu//base:aemu-base`.

## Threading Model
* **Thread Safe:** Detection functions are generally stateless or use local variables.
