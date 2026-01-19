# Component: Imaging

**Role:** Utilities for pixel format definitions, conversions, and basic 2D geometry (Rects) related to images.
**Location:** `hardware/generic/goldfish/emulator/libs/imaging`
**Namespace:** `goldfish::imaging`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `AndroidPixelFormat` | `@goldfish//emulator/libs/imaging` | `.../android_pixel_format.h` | Enums for Android-specific pixel formats (AHardwareBuffer). |
| `ImageRef` | `@goldfish//emulator/libs/imaging` | `.../image_ref.h` | Non-owning reference to raw image data (width, height, stride, pointer). |
| `ToVkFormat` | `@goldfish//emulator/libs/imaging` | `.../to_vk_format.h` | Conversion from internal/Android formats to `VkFormat`. |

## Critical Infrastructure
* **Format Conversion:** Maps legacy or Android formats to modern Vulkan equivalents.
* **Geometry:** `Rect` struct for simple 2D operations.

## Dependencies
* **External:** `@vulkan-headers`.

## Threading Model
* **Thread Safe:** All functions and classes are pure data containers or stateless helpers.
