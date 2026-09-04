# Component: Android Sockets

**Role:** Cross-platform wrappers for BSD sockets.
**Location:** `hardware/generic/goldfish/emulator/libs/sockets`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `SocketUtils` | `:sockets` | `include/android/sockets/socket_utils.h` | Exports functions like `socketCreateTcp4`, `socketAccept`, `socketRecv`. |
| `ScopedSocket` | `:sockets` | `include/android/sockets/scoped_socket.h` | RAII wrapper around socket descriptors. |

## Critical Infrastructure
* **Platform Abstraction:** Handles differences between POSIX sockets and Windows Winsock (`WSA*`, `closesocket` vs `close`).
* **Loopback:** `socketTcp4LoopbackServer`/`Client` provides easy creation of localhost connections.
* **SocketPair:** Implements `socketpair` for Windows using a temporary TCP connection on the loopback interface (since Windows lacks native unix domain sockets for `select`).

## Dependencies
* **System:** `//emulator/libs/system`.
* **Abseil:** `@abseil-cpp//absl/log`.

## Threading Model
* **Thread Safe:** Socket file descriptors are thread-safe OS resources.
