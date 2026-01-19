# Component: Android Process

**Role:** Utilities for spawning and managing child processes.
**Location:** `hardware/generic/goldfish/android/process`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `Command` | `:process` | `.../command.h` | Builder for creating and executing child processes. |
| `ObservableProcess` | `:process` | `.../process.h` | Represents a running child process (PID, exit code, I/O). |
| `Process` | `:process` | `.../process.h` | Base class for process information (PID, Exe). |

## Critical Infrastructure
* **Command Builder:** Fluent API (`Command::Create({"ls", "-l"}).Execute()`) to launch processes. Supports redirection of stdout/stderr, daemonization, and environment inheritance.
* **Observability:** `ObservableProcess` allows capturing stdout/stderr, waiting for completion (with timeout), and killing the process.
* **Platform Abstraction:** Hides differences between POSIX (`fork`/`exec`) and Windows (`CreateProcess`).

## Dependencies
* **System:** `//android/system`.
* **Base:** `@abseil-cpp`.

## Threading Model
* **Thread Safe:** `Command` objects are not thread-safe. `ObservableProcess` methods are generally thread-safe.
* **Overseer:** If output capture is enabled, a dedicated thread is spawned to read from the child's pipes.
