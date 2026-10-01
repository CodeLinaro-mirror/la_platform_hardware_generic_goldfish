# Component: Status Macros & Utilities

**Role:** Convenience macros and type traits for handling `absl::Status` and `absl::StatusOr`.
**Location:** `hardware/generic/goldfish/emulator/libs/status_macros`
**Namespace:** `android::status` (type traits), global (macros)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `RETURN_IF_ERROR(expr)` | `:status_macros` | `.../status_macros.h` | Returns early if `expr` evaluates to a non-OK status. |
| `ASSIGN_OR_RETURN(lhs, rhs)` | `:status_macros` | `.../status_macros.h` | Assigns value of `StatusOr` to `lhs`, or returns early on error. |
| `flatten_status_or_t<T>` | `:flatten` / `:status_macros` | `.../flatten.h` | Flattens return types to eliminate nested `StatusOr` wrappers. |

## Critical Infrastructure
* **Error Propagation:** Reduces boilerplate when propagating errors up the call stack. Matches the style of similar macros in other Google projects (Perfetto, TensorFlow).
* **Return Type Flattening:** Type trait (`flatten_status_or_t<T>`) used by generic wrappers (e.g. `EventLoop::PostAndWait`) to cleanly unwrap nested `StatusOr` results.

## Dependencies
* **Core:** `@abseil-cpp//absl/status`, `@abseil-cpp//absl/status:statusor`.

## Threading Model
* **Thread Safe:** Macros expand to standard C++ control flow; type traits are pure compile-time metaprogramming.
