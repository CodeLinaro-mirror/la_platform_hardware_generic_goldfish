# Android CPU Acceleration and Virtualization Utilities**

This package provides utilities essential for determining hardware acceleration capabilities and CPU features. It's designed to aid in making informed decisions about the best acceleration approach for optimal Android emulation.

## Key Functionalities

* **Acceleration Status Detection:**
  * Determines whether hardware acceleration is available on the host machine.
  * Identifies supported acceleration technologies (e.g., KVM, HAXM, Hypervisor.framework, WHPX)
  * Provides detailed status information and explanations to guide developers.

* **CPU Feature Identification:**
  * Reports CPU vendor (Intel, AMD, or others).
  * Detects whether the CPU supports virtualization technologies and advanced instruction sets (SSE, SSE2, SSE3, etc.)
  * Determines if the host is running in a virtual machine environment.

* **Hypervisor Support:**
  * Checks the status of the Hyper-V hypervisor on Windows systems.
  * Checks the availability and support for Apple's Hypervisor.framework on macOS systems.

## Example Usage

```c++
#include "android/goldfish/cpu/CpuAccelerator.h"

int main() {
    if (GetCurrentCpuAccelerator() != CPU_ACCELERATOR_NONE) {
        std::cout << "Hardware acceleration is available: "
                  <<  GetCurrentCpuAcceleratorStatus() << std::endl;
    } else {
        std::cout << "Hardware acceleration is not supported: "
                  << GetCurrentCpuAcceleratorStatus() << std::endl;
    }
}
