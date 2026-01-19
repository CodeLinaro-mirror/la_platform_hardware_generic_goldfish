# Component: CPP Utils

**Role:** Provides modern C++ meta-programming utilities and helpers.
**Location:** `hardware/generic/goldfish/emulator/libs/cpp`
**Namespace:** `goldfish::cpp`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Overloaded` | `@goldfish//emulator/libs/cpp` | `include/goldfish/cpp/overloaded.h` | Helper struct for `std::visit` on variants. |

## Critical Infrastructure
* **Overloaded:** Inherits from a variadic pack of lambdas/functors and brings their `operator()` into scope. This allows defining a visitor in-place.

## Threading Model
* **Stateless:** These templates are stateless and thread-safe.

## Usage Example
```cpp
std::visit(Overloaded{
    [](int i) { ... },
    [](float f) { ... },
}, my_variant);
```
