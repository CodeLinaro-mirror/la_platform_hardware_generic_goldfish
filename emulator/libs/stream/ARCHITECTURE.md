# Component: Stream

**Role:** Provides thread-safe implementations of `std::streambuf` for inter-thread communication and synchronization.
**Location:** `hardware/generic/goldfish/emulator/libs/stream`
**Namespace:** `goldfish`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `BlockingStreamBuf` | `@goldfish//emulator/libs/stream` | `include/goldfish/blocking_stream_buf.h` | A blocking, thread-safe stream buffer for producer-consumer patterns. |
| `SynchronizedStreamBuf` | `@goldfish//emulator/libs/stream` | `include/goldfish/synchronized_stream_buf.h` | A decorator that adds thread safety to an existing `std::streambuf`. |

## Critical Infrastructure

### `BlockingStreamBuf`
*   **Responsibility:** Implements a producer-consumer queue using `std::streambuf` interface.
*   **Logic:**
    *   **Writer:** Writes are appended to an internal `std::deque`.
    *   **Reader:** Reads block until data is available or the stream is closed.
    *   **Locking:** Uses `absl::Mutex` to protect the internal buffer.
*   **Usage:** Useful for piping output from one thread to an input stream in another thread (e.g., logging, command piping).

### `SynchronizedStreamBuf`
*   **Responsibility:** Wraps a raw `std::streambuf` pointer and guards all access with a mutex.
*   **Logic:** Delegates all `overflow`, `xsputn`, `underflow`, etc., calls to the inner buffer while holding a lock.
*   **Usage:** When multiple threads need to write to the same underlying buffer (like `std::cout`'s buffer) without interleaving at the character level, or when an underlying buffer is not thread-safe.

## Dependencies
*   **External:** `@abseil-cpp//absl/synchronization` (Mutex, MutexLock, Condition).

## Threading Model
*   **Thread Safe:** Both classes are designed explicitly for concurrent access.
*   **Blocking:** `BlockingStreamBuf` readers will block waiting for data.