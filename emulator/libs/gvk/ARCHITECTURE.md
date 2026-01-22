# Component: Goldfish Vulkan (GVK)

**Role:** A C++ wrapper and utility layer for the Vulkan API, tailored for Goldfish.
**Location:** `hardware/generic/goldfish/emulator/libs/gvk`
**Namespace:** `goldfish::gvk`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `DeviceDispatch` | `@goldfish//emulator/libs/gvk` | `.../device_dispatch.h` | Table of function pointers for a `VkDevice`. |
| `InstanceDispatch` | `@goldfish//emulator/libs/gvk` | `.../instance_dispatch.h` | Table of function pointers for a `VkInstance`. |
| `StagingBuffers` | `@goldfish//emulator/libs/gvk` | `.../staging_buffers.h` | Helper for managing host-visible staging memory. |

## Critical Infrastructure
* **Dispatch Tables:** Simplifies loading and calling Vulkan entry points (avoiding manual `vkGetProcAddr`).
* **Helpers:** Utilities for selecting physical devices (`select_physical_device`), finding memory types (`get_memory_type_index`), and queue creation.

## Dependencies
* **External:** `@vulkan-headers` (Official Vulkan API).
* **Internal:** `//emulator/libs/os` (Library loading).

## Threading Model
* **Thread Safety:** Most helpers are stateless. `DeviceDispatch`/`InstanceDispatch` are typically initialized once per object. Vulkan's own threading rules apply to the underlying API calls.
