# Component: OS Utils

**Role:** Operating System abstractions, primarily for dynamic library loading.
**Location:** `hardware/generic/goldfish/emulator/libs/os`
**Namespace:** `goldfish::os`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `DynamicLibrary` | `@goldfish//emulator/libs/os` | `include/goldfish/os/dynamic_library.h` | Cross-platform wrapper for `dlopen`/`dlsym` (or `LoadLibrary`/`GetProcAddress`). |
| `TEMP_FAILURE_RETRY` | `@goldfish//emulator/libs/os` | `include/goldfish/os/temp_failure_retry.h` | Macro to retry syscalls on `EINTR`. |

## Critical Infrastructure
* **DynamicLibrary:** Uses RAII (`UniqueHandle` internally) to manage the library handle. Returns function pointers via `resolve()`.

## Threading Model
* **Thread Safe:** `DynamicLibrary` loading is generally thread-safe (OS dependent), but using the returned function pointers depends on the library being loaded.
