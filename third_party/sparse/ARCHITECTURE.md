# Component: Sparse Image Library

**Role:** Library for reading, writing, and manipulating Android sparse image files (.simg).
**Location:** `hardware/generic/goldfish/third_party/sparse`
**Namespace:** C API (global functions `sparse_file_*`)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `sparse_file` | `:sparse` | `.../sparse.h` | Opaque handle representing a sparse file in memory. |
| `img2simg` | `:img2simg` | N/A (Binary) | Tool to convert a raw image to a sparse image. |

## Critical Infrastructure
* **Sparse Format:** Supports the Android sparse image format, which optimizes storage by encoding chunks of zeros efficiently and adding checksums.
* **Operations:**
    *   **Import:** Read raw or sparse files into memory structures.
    *   **Write:** Serialize to a file descriptor, optionally gzipped.
    *   **Resparse:** Split large sparse files into multiple smaller ones (for flashing limits).

## Dependencies
* **External:** `@zlib`.
* **System:** `//third_party/windows/mman` (Windows mmap compat).

## Threading Model
* **Thread Safe:** Library functions are generally reentrant but operate on specific `sparse_file` handles which are not thread-safe.
