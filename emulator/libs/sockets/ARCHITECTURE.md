# Component: Android Sockets

**Role:** Cross-platform wrappers for BSD sockets.
**Location:** `hardware/generic/goldfish/android/sockets`
**Namespace:** `android::base`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `SocketUtils.h` | `:sockets` | `@aemu//base/sockets/SocketUtils.h` | Exports functions like `socketCreateTcp4`, `socketAccept`, `socketRecv`. |

## Critical Infrastructure
* **Platform Abstraction:** Handles differences between POSIX sockets and Windows Winsock (`WSA*`, `closesocket` vs `close`).
* **Loopback:** `socketTcp4LoopbackServer`/`Client` provides easy creation of localhost connections.
* **SocketPair:** Implements `socketpair` for Windows using a temporary TCP connection on the loopback interface (since Windows lacks native unix domain sockets for `select`).

## Dependencies
* **System:** `//emulator/libs/system`.
* **Base:** `@aemu//base:aemu-base` (for `EintrWrapper` etc).

## Threading Model
* **Thread Safe:** Socket file descriptors are thread-safe OS resources.
