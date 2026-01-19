# gRPC Request Flow

This document details the lifecycle of an incoming gRPC request (e.g., `sendMouse`) from an external client.

## Flow Diagram

```mermaid
sequenceDiagram
    participant Client as External Client
    participant Server as gRPC Server
    participant Interceptor as Interceptors (Auth/Log)
    participant Service as EmulatorControllerImpl
    participant Async as QemuEventLoop
    participant QEMU as QEMU Main Loop (BQL)

    Client->>Server: RPC: sendMouse(x, y)

    Note over Server, Interceptor: gRPC Thread Pool

    Server->>Interceptor: Intercept()
    Interceptor->>Interceptor: Check Token / Log
    Interceptor->>Service: Proceed()

    Service->>Service: InputEventSender::send()

    Service->>Async: EventLoop::Post(Task)
    Note right of Async: Marshals to QEMU thread
    Async-->>Service: Future/Promise

    Note over QEMU: QEMU Main Thread
    Async->>QEMU: Execute Task
    QEMU->>QEMU: Inject Input (qemu_input_event_send)

    Async-->>Service: Task Complete (Status)
    Service-->>Client: RPC Response (OK)
```

## Key Mechanisms

1.  **Interception:** Before the service logic runs, the request passes through the `AuthMetadataProcessor` (Token check) and `LoggingInterceptor` (Audit).
2.  **Marshalling:** Most emulator state (VM, Devices) is protected by the **Big QEMU Lock (BQL)** and must be accessed from the main thread. The service implementation uses `goldfish::async::EventLoop` (specifically `QemuEventLoop`) to post a task to that thread.
3.  **Synchronization:** Synchronous RPCs block the gRPC thread waiting for the QEMU task to complete. Asynchronous/Streaming RPCs use reactors to handle completions without blocking.
