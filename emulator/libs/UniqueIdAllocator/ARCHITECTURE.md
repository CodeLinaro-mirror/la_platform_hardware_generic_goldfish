# Component: Unique ID Allocator

**Role:** Manages the allocation and recycling of unique 32-bit integers.
**Location:** `hardware/generic/goldfish/emulator/libs/UniqueIdAllocator`
**Namespace:** `goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `UniqueIdAllocator` | `@goldfish//emulator/libs/UniqueIdAllocator` | `include/goldfish/unique_id_allocator.h` | The allocator class. |

## Critical Infrastructure
* **Recycling:** Tracks returned IDs in a `std::set` to reuse them, ensuring compactness.
* **Snapshotting:** Supports serialization via `goldfish::archive::IWriter/IReader` to persist state across emulator sessions.

## Dependencies
* **Internal:** `//emulator/libs/archive` (Serialization).

## Threading Model
* **Not Thread Safe:** Methods `Get`, `Put`, and snapshot operations are not synchronized. External locking is required for concurrent access.
