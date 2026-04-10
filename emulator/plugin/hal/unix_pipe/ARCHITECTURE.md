# Component: Unix Pipe HAL

**Role:** Proxies a guest connection to a local Unix Domain Socket on the host.
**Location:** `hardware/generic/goldfish/emulator/hal/unix_pipe`
**Namespace:** `goldfish::devices::unix_pipe`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `UnixPipeDevice` | `:unix_pipe` | `.../unix_pipe.h` | The HAL implementation. |

## Critical Infrastructure
* **Role:** Acts as a transparent proxy.
* **Mechanism:**
    1.  Guest connects to the `unix-pipe` service with an argument (the path to the host socket).
    2.  `UnixPipeDevice` attempts to connect to that path on the host using `async::LibuvSocket`.
    3.  If successful, it shuttles bytes bi-directionally between the Guest `HalSocket` and the Host `AsyncSocket`.

## Dependencies
* **Core:** `//emulator/hal/plug`.
* **Async:** `//emulator/libs/async:libuv_sockets` (Host-side I/O).

## Threading Model
* **Thread Safe:** Inherits `HalPlug`. Callbacks serialized on `EventLoop`.
* **Async I/O:** Uses non-blocking sockets on the host side, ensuring the event loop is never blocked waiting for I/O.
