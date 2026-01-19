# Backpressure & Flow Control

This flow details how `AsyncSocket` handles backpressure. The mechanism is **Consumer-Driven**: the entity consuming data from the socket (e.g., a proxy, a ring buffer) is responsible for explicitly signaling when to stop or resume reading.

## Mechanism
1.  **Stop:** When the consumer is overwhelmed, it calls `OnFlowControlEvent(false)`. The socket triggers `uv_read_stop`.
2.  **Pause:** Libuv stops pulling data from the OS kernel. The kernel buffer fills up, eventually triggering standard TCP Window clamping (flow control) at the network layer.
3.  **Resume:** When the consumer drains, it calls `OnFlowControlEvent(true)`. The socket triggers `uv_read_start`.

## Flow Diagram

```mermaid
sequenceDiagram
    participant Consumer as Data Consumer
    participant Socket as LibuvSocket
    participant Loop as EventLoop
    participant Libuv as libuv (C-Library)
    participant Kernel as OS Kernel

    Note over Consumer, Kernel: Scenario: Consumer buffer is full

    Consumer->>Socket: OnFlowControlEvent(enable_reading=false)
    Socket->>Loop: Post(Task)

    loop Async Task Execution
        Loop->>Socket: Task()
        Socket->>Libuv: uv_read_stop(stream)
    end

    Note right of Libuv: Libuv stops alloc/read callbacks.
    Note right of Kernel: Kernel buffer fills up.<br/>TCP Window Size -> 0.

    Note over Consumer, Kernel: ...Time Passes (Consumer Drains)...

    Consumer->>Socket: OnFlowControlEvent(enable_reading=true)
    Socket->>Loop: Post(Task)

    loop Async Task Execution
        Loop->>Socket: Task()
        Socket->>Libuv: uv_read_start(stream, ...)
    end

    Libuv->>Kernel: Resume Polling
    Kernel->>Libuv: Data Available
    Libuv->>Socket: OnRead(...)
    Socket->>Consumer: on_read_callback(...)
```

## Key Components

*   **`OnFlowControlEvent`**: Thread-safe entry point. Can be called from any thread; uses `EventLoop::Post` to marshal to the IO thread.
*   **`uv_read_stop`**: The libuv primitive that effectively pauses the stream without closing it.
