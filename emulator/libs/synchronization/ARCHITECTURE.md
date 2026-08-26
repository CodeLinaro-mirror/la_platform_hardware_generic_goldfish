# Component: Synchronization

**Role:** Thread synchronization primitives and lock annotations.
**Location:** `hardware/generic/goldfish/emulator/libs/synchronization`
**Namespace:** `goldfish::synchronization`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Mutex` | `:synchronization` | `include/goldfish/synchronization/mutex.h` | Clang thread safety annotated mutex wrappers around `absl::Mutex`. |

## Critical Infrastructure
* **Thread Safety Annotations:** Integrates Clang static analysis annotations (`GUARDED_BY`, `LOCKS_EXCLUDED`, `REQUIRES`) to catch race conditions and lock ordering violations at compile time.

## Dependencies
* **Core:** `@abseil-cpp//absl/synchronization`.

## Threading Model
* **Thread Safe:** Concurrency control primitives.
