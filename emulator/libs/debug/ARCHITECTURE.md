# Component: Debug Utils

**Role:** Provides standardized macros for assertions, logging failures, and returning error codes.
**Location:** `hardware/generic/goldfish/emulator/libs/debug`
**Namespace:** Global Macros

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `debug.h` | `@goldfish//emulator/libs/debug` | `include/goldfish/debug.h` | Macros `FAILURE(Val)`, `FAILURE_STR(Msg, Val)`, `NOT_NULL(Ptr)`. |

## Critical Infrastructure
* **FAILURE(Val):** Logs an error with the function name and returns `Val`. Useful for one-line error handling.
* **NOT_NULL(Ptr):** Asserts pointer is not null in debug builds, passes through in release.

## Dependencies
* **External:** `@abseil-cpp//absl/log`.

## Threading Model
* **Thread Safe:** Relies on `absl::log`, which is thread-safe.
