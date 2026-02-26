# Component: ProtoDataStore

**Role:** High-performance, zero-indirection circular storage for generic Protobuf messages.
**Location:** `hardware/generic/goldfish/emulator/libs/proto_data_store`
**Namespace:** `goldfish::proto_data_store`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `RawCircularLog` | `:proto_data_store` | `include/goldfish/raw_circular_log.h` | Low-level, byte-agnostic circular log engine. |
| `ProtoCircularLog<T>` | `:proto_data_store` | `include/goldfish/circular_message_log.h` | Type-safe, templated Protobuf wrapper. |
| `CircularMessageLog` | `:proto_data_store` | `include/goldfish/circular_message_log.h` | Type-erased Protobuf log for runtime polymorphism. |

## Critical Infrastructure
* **Dual-Layer Architecture:** Decouples memory management and eviction logic (`RawCircularLog`) from Protobuf serialization (`ProtoCircularLog`). This allows for cleaner code and specialized performance optimizations in the core engine.
* **Zero-Indirection:** The library provides concrete classes with no virtual methods (no vtable). This minimizes latency on the gRPC hot-path by avoiding dynamic dispatch.
* **Crash-Resilient Commit Protocol:** Every record is managed via a two-phase protocol:
    1. **Uncommitted:** Space is reserved and the header is written with `commit = 0`.
    2. **Serialize:** The payload is written directly into the log via a callback.
    3. **Committed:** The header is atomically updated with `commit = 1`.
  Readers (`ForEach`) stop iterating when they encounter a record where `commit` is 0. This ensures they never see partial writes, but it also means that any valid records written *after* a crash-interrupted write (in the same segment) will be inaccessible.
* **O(1) Statistics:** The log maintains a persistent message count and tail pointer, allowing for constant-time complexity for `MessageCount()` and rapid recovery after a crash.
* **Semantic Factories:** Supports two distinct process roles via named factories:
    * `CreateWriter()`: For the process owning/initializing the memory.
    * `CreateReader()`: For observer processes or post-mortem analysis tools.
* **Zero-Fill at Wrap:** When the buffer wraps, the remaining space is zeroed to prevent stale data from being misinterpreted as valid messages by recovery scanners.

## Dependencies
* **Internal:** `//emulator/libs/debug`, `@abseil-cpp//absl/synchronization`, `@abseil-cpp//absl/log:check`.
* **External:** `@com_google_protobuf//:protobuf`.

## Threading Model
* **Thread Safe:** All public methods are protected by an internal `absl::Mutex`.
* **Optimized for Micro-Workloads:** Benchmarking proved that for small messages (e.g., gRPC breadcrumbs), a single mutex-protected log provides higher total system throughput (up to **130 MiB/s**) than sharded implementations due to reduced management overhead and better cache locality.
