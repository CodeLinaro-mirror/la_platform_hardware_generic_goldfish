# Component: Android Filesystems

**Role:** Utilities for creating and resizing ext4 filesystem images.
**Location:** `hardware/generic/goldfish/android/filesystems`
**Namespace:** `android::filesystems`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `resizeExt4Partition` | `:filesystems` | `.../ext4_resize.h` | Resizes an existing ext4 partition file (invokes `resize2fs`). |
| `android_createEmptyExt4Image` | `:filesystems` | `.../ext4_utils.h` | Creates a new empty ext4 image (invokes `mkuserimg_mke2fs` or similar). |

## Critical Infrastructure
* **Process Invocation:** These functions typically spawn external binaries (`resize2fs`, `mkuserimg_mke2fs`) to perform the heavy lifting.
* **Validation:** Checks partition size limits (min 128 MiB, max 16 TiB).

## Dependencies
* **External Tools:** Requires `resize2fs` and `mke2fs` tools to be available in the environment or bundle.
* **System:** `//android/system`, `//android/process`.

## Threading Model
* **Thread Safe:** Functions are stateless wrapper calls.
