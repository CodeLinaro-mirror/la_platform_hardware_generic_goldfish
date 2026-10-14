# Component: SocketBuffer

**Role:** A dynamic, auto-resizing ring buffer designed for buffering streaming data (like sockets).
**Location:** `hardware/generic/goldfish/emulator/libs/SocketBuffer`
**Namespace:** `goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `SocketBuffer` | `@goldfish//emulator/libs/SocketBuffer` | `include/goldfish/socket_buffer.h` | The ring buffer class. |
| `socket_buffer_perf` | `@goldfish//emulator/libs/SocketBuffer:socket_buffer_perf` | N/A | Google Benchmark suite measuring drain, wrap-around, and ADB burst throughput. |

## Critical Infrastructure
| File | Responsibility |
| :--- | :--- |
| `socket_buffer.cc` | Implements the circular buffer logic, auto-resizing, and snapshot serialization. |
| `socket_buffer_perf.cc` | Benchmarks `Append`, `Peek`, and `Consume` across payload sizes (1 KiB to 32 MiB). |

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
*   **Auto-Resize:** If `Append` exceeds capacity, the buffer grows (`max(64 KiB, 2 * current_capacity, 1.5 * new_size)`) and data is linearized (unrolled) into the new allocation.
*   **Contiguity:** `Peek` returns the largest contiguous chunk available starting at `consume_`. It may return less than `Size()` if the data wraps around the end of the array.

### Memory Management
*   **Growth:** Allocates on demand (minimum `64 KiB`).
*   **Shrinkage:** Frees backing memory if `Consume` empties the buffer and capacity is at least `kLargeCapacityReleaseIfEmpty` (`16 MiB`).
