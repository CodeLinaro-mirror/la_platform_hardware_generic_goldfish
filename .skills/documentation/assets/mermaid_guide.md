# Mermaid Diagram Standards

When tracing flows, use these patterns to highlight architectural constraints.

## Pattern 1: Sequence with Threading and Locks

Use `Note` to indicate locking and `par`/`alt` for complex logic.

```mermaid
sequenceDiagram
    participant Guest as Guest OS
    participant Render as RenderThread
    participant Shared as SharedMemory
    participant Net as gRPC Service

    Guest->>Render: glReadPixels()
    activate Render

    Note right of Render: ACQUIRE LOCK (Context)
    Render->>Render: Read GPU Buffer

    rect rgb(200, 150, 150)
        Note over Render, Shared: CRITICAL SECTION
        Render->>Shared: memcpy(pixels)
    end

    Note right of Render: RELEASE LOCK

    Render->>Net: Notify(Ready)
    deactivate Render

    Net->>Net: Serialize Protobuf
    Net->>Client: Send()
```

## Pattern 2: Component Data Flow

Use specific shapes to denote component types.

* `[]` Square: Passive Data / Buffer
* `()` Round: Active Thread / Process
* `{}` Rhombus: Logic Gate / Decision

```mermaid
graph TD
    A[Guest Framebuffer] -- virtio --> B(RenderThread)
    B -- Lock --> C{Is Screenshot?}
    C -- Yes --> D[Shared Memory Ring]
    C -- No --> E(Display Surface)
```
