# Socket Read Lifecycle

This flow details how `LibuvSocket` (in `libuv_sockets.cc`) handles asynchronous reads, bridging C-style libuv callbacks to C++ `std::function` callbacks.

## Flow Diagram

```mermaid
sequenceDiagram
    participant User as Consumer
    participant Socket as LibuvSocket
    participant Alloc as ReadBufferAllocator
    participant Libuv as libuv (C-Library)

    Note over User, Libuv: Thread: EventLoop Thread

    User->>Socket: SetOnReadCallback(cb)
    User->>Socket: StartReading()
    Socket->>Libuv: uv_read_start(stream, AllocCallback, ReadCallback)

    loop Event Loop Cycle
        Libuv->>Socket: AllocCallback(handle, suggested_size)
        Socket->>Alloc: Alloc(suggested_size)
        Alloc-->>Libuv: uv_buf_t (buffer)

        Note right of Libuv: Data arrives from OS...

        Libuv->>Socket: ReadCallback(stream, nread, buffer)

        alt nread > 0 (Success)
            Socket->>User: on_read_(buffer, OkStatus)
        else nread == UV_EOF (End of Stream)
            Socket->>Socket: Close()
        else nread < 0 (Error)
            Socket->>User: on_read_(empty, ErrorStatus)
            Socket->>Socket: Close()
        end

        Socket->>Alloc: Free(buffer)
    end
```

## Key Components

*   **`LibuvSocket::StartReading`**: Registers the static lambdas with libuv.
*   **`ReadBufferAllocator`**: Manages a small pool of buffers to minimize `malloc/free` overhead.
*   **`uv_read_start`**: The libuv primitive that triggers the reactor pattern.
