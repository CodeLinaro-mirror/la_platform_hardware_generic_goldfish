# Server Connection Lifecycle

This flow details how `AsyncSocketServer` (via `LibuvAsyncSocketFactory`) creates a listening socket and handles incoming connections, bridging libuv's `uv_listen` to the C++ `OnConnectCallback`.

## Flow Diagram

```mermaid
sequenceDiagram
    participant User as Consumer
    participant Factory as LibuvAsyncSocketFactory
    participant Server as LibuvServer (Tcp/Un)
    participant Client as LibuvSocket (Child)
    participant Libuv as libuv (C-Library)

    Note over User, Libuv: Thread: EventLoop Thread

    %% Setup Phase
    User->>Factory: CreateServer(loop, endpoint, ConnectCallback)
    Factory->>Server: Create(loop, endpoint, cb)
    Server->>Libuv: uv_tcp_init / uv_pipe_init
    Server->>Libuv: uv_tcp_bind / uv_pipe_bind2
    Server->>Libuv: uv_listen(stream, backlog, ListenCallback)
    Libuv-->>Server: (Success)
    Server-->>User: shared_ptr<AsyncSocketServer>

    %% Connection Phase
    Note right of Libuv: Incoming Connection Request...
    Libuv->>Server: ListenCallback(stream, status)

    Server->>Server: OnNewConnection(stream)
    create participant Client
    Server->>Client: make_shared<LibuvSocket>()

    Server->>Client: Accept(server_stream, client_stream)
    Client->>Libuv: uv_accept(server_stream, client_stream)

    alt Accept Success
        Server->>User: ConnectCallback(shared_ptr<AsyncSocket>)

        alt User Keeps Socket
            User->>Client: SetOnReadCallback(...)
            User->>Client: StartReading()
        else User Rejects/Ignores
            Note right of User: shared_ptr goes out of scope
            Client->>Client: Close()
        end
    else Accept Failed
        Client->>Client: Close()
    end
```

## Key Components

*   **`LibuvAsyncSocketFactory::CreateServer`**: The static entry point that dispatches to TCP or Unix Domain Socket implementations based on the `Endpoint` type.
*   **`uv_listen`**: Puts the socket in a passive mode to wait for incoming connections.
*   **`uv_accept`**: Must be called immediately in the connection callback to finalize the handshake.
*   **Ownership Transfer**: The `ConnectCallback` passes a `std::shared_ptr<AsyncSocket>` to the user. If the user does not store this pointer, the socket is automatically closed and destroyed.
