# Component: Goldfish Base

**Role:** Provides foundational C++ utilities for resource management (Smart Pointers, RAII).
**Location:** `hardware/generic/goldfish/emulator/libs/base`
**Namespace:** `goldfish::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `IntrusivePtr` | `@goldfish//emulator/libs/base` | `include/goldfish/base/intrusive_ptr.h` | Smart pointer for objects with internal ref-counting. |
| `UniqueHandle` | `@goldfish//emulator/libs/base` | `include/goldfish/base/unique_handle.h` | Generic RAII wrapper for handles (FDs, C-pointers). |

## Critical Infrastructure
* **Zero Overhead:** These classes are designed to have zero runtime overhead compared to manual management (template-based).
* **ADL Support:** `IntrusivePtr` relies on Argument-Dependent Lookup (ADL) to find `IntrusivePtrAddRef` and `IntrusivePtrRelease` for the managed type.

## Dependencies
* **Standard Library:** Only depends on C++ standard headers (`<functional>`, `<utility>`).

## Threading Model
* **Thread Safety:** The smart pointer classes themselves are **not** thread-safe (like `std::shared_ptr`). Accessing the *same* smart pointer instance from multiple threads requires synchronization.
* **Ref-Counting:** The thread-safety of the reference counting depends on the implementation of `IntrusivePtrAddRef`/`IntrusivePtrRelease` provided by the user (usually atomic).
