# Component: Android Memory

**Role:** Utilities for memory tracking, hinting, and manipulation.
**Location:** `hardware/generic/goldfish/android/memory`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `MemoryTracker` | `:memory` | `memory_tracker.h` | Tracks memory usage by group (if `AEMU_TCMALLOC_ENABLED`). |
| `memoryHint` | `:memory` | `.../MemoryHints.h` | Wraps `madvise` / `VirtualUnlock` / `mprotect` for performance tuning. |
| `ContiguousRangeMapper` | `:memory` | `.../ContiguousRangeMapper.h` | Helper to apply operations over discontiguous memory ranges as if they were contiguous. |

## Critical Infrastructure
* **Memory Hints:** Provides a cross-platform API (`MemoryHint::DontNeed`, `MemoryHint::Random`) to advise the OS kernel about memory access patterns. This is crucial for optimizing emulator performance and reducing host memory footprint.
* **ContiguousRangeMapper:** Used to break down large operations (like touching memory pages) into page-aligned chunks.

## Dependencies
* **System:** `//android/system` (OS calls).
* **Base:** `@aemu//base:aemu-base`.

## Threading Model
* **Thread Safe:** `MemoryTracker` uses `std::atomic` for stats. `memoryHint` functions are stateless wrappers around syscalls.
