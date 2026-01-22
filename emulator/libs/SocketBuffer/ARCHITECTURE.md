# Component: SocketBuffer

**Role:** A dynamic, auto-resizing ring buffer designed for buffering streaming data (like sockets).
**Location:** `hardware/generic/goldfish/emulator/libs/SocketBuffer`
**Namespace:** `goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `SocketBuffer` | `@goldfish//emulator/libs/SocketBuffer` | `include/goldfish/socket_buffer.h` | The ring buffer class. |

## Critical Infrastructure
| File | Responsibility |
| :--- | :--- |
| `socket_buffer.cc` | Implements the circular buffer logic, auto-resizing, and snapshot serialization. |

## Dependencies
* **Internal:** `//emulator/libs/archive` (Serialization/Snapshot support).
* **Internal:** `//emulator/libs/debug` (Assertions/Logging).

## Threading Model
* **Not Thread Safe:** The `SocketBuffer` class is **not** thread-safe.
* **Usage Rule:** It is intended to be used within a single thread (e.g., an IO loop) or guarded by an external mutex.

## Logic & Algorithms
### Circular Buffer
*   **Data Structure:** Uses a `std::unique_ptr<char[]>` as a backing array.
*   **Pointers:** Maintains `produce_` (write head) and `consume_` (read head) indices.
*   **Auto-Resize:** If `Append` exceeds capacity, the buffer grows (typically 1.5x) and data is linearized (unrolled) into the new allocation.
*   **Contiguity:** `Peek` returns the largest contiguous chunk available starting at `consume_`. It may return less than `Size()` if the data wraps around the end of the array.

### Memory Management
*   **Growth:** Allocates on demand.
*   **Shrinkage:** Can optionally free memory if `Consume` empties the buffer and capacity is large (`> 4MB`).
