# Component: Status Macros

**Role:** Convenience macros for handling `absl::Status` and `absl::StatusOr`.
**Location:** `hardware/generic/goldfish/android/status_macros`
**Namespace:** Global macros

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `RETURN_IF_ERROR(expr)` | `:status_macros` | `.../status_macros.h` | Returns early if `expr` evaluates to a non-OK status. |
| `ASSIGN_OR_RETURN(lhs, rhs)` | `:status_macros` | `.../status_macros.h` | Assigns value of `StatusOr` to `lhs`, or returns early on error. |

## Critical Infrastructure
* **Error Propagation:** Reduces boilerplate when propagating errors up the call stack. Matches the style of similar macros in other Google projects (Perfetto, TensorFlow).

## Dependencies
* **Core:** `@abseil-cpp//absl/status`.

## Threading Model
* **Thread Safe:** Macros expand to standard C++ control flow.
