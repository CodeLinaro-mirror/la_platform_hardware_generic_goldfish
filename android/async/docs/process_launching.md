# Process Launching

The `UvProcessLauncher` provides a high-level C++ wrapper around libuv's `uv_spawn`. It abstracts the complexity of argument marshalling and stdio redirection.

## Features

*   **Daemonization:** Can launch processes that persist after the parent exits (`UV_PROCESS_DETACHED`).
*   **Stdio Inheritance:** Configurable option (`keep_stdio`) to pass `stdout`/`stderr` to the child.
*   **Type Safety:** Uses `std::filesystem::path` and `std::vector<string>` instead of raw C-arrays.

## Flow

```mermaid
sequenceDiagram
    participant User
    participant Launcher as UvProcessLauncher
    participant Libuv

    User->>Launcher: Launch(LaunchConfig)

    Launcher->>Launcher: Marshall Args (vector -> char**)
    Launcher->>Launcher: Configure Stdio (uv_stdio_container_t)

    Launcher->>Libuv: uv_spawn(options)

    alt Daemon
        Launcher->>Libuv: uv_unref(process_handle)
        Note right of Libuv: Loop won't wait for child
    end

    Launcher-->>User: ProcessHandle
```
