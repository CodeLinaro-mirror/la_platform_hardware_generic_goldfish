# DESIGN: The Cable, Vsock, and HAL Plug Abstractions

This document details the design of the `cable` abstraction, its concrete implementation in the `vsock` plugin, and the thread-safe `HalPlug` layer built on top of it. The central challenge this system solves is bridging thread-safe, modern C++ components (HALs) with the single-threaded, C-style, lock-sensitive environment of the QEMU main loop.

## 1. The `cable` Abstraction: A Decoupled Protocol

The core of the design is the `cable` interface, which decouples the logic of a virtual device from the logic of its connection transport. It defines two primary interfaces: `IPlug` and `ISocket`.

### Core Components

-   **`IPlug`**: Represents the *device* or *service* end of the connection. It receives events from the transport layer.
    -   `onConnect()`: The connection is ready.
    -   `onReceive()`: Data has arrived from the guest.
    -   `onUnplug()`: The guest has closed the connection.
    -   Managed via `PlugPtr` (`std::shared_ptr<IPlug>`), indicating shared ownership. The connection manager holds a reference to the plug, and other parts of the system might as well.

-   **`ISocket`**: Represents the *transport* or *pipe* end of the connection. It's the handle the `IPlug` uses to send data and control the connection.
    -   `sendAsync()`: Sends data to the guest.
    -   `switchPlug()`: Replaces the current `IPlug` with another one.
    -   `unplug()`: Closes the connection from the host side.
    -   Managed via `SocketPtr` (`std::unique_ptr<ISocket, Unplugger>`), indicating unique ownership. The `IPlug` exclusively owns its `ISocket`.

### The Central Pattern: Managed Circular Dependency

The key to this design is a deliberate, managed circular dependency:

1.  The `IPlug` implementation holds a `SocketPtr` to communicate with the transport.
2.  The transport implementation (the "socket manager," e.g., `vsock`) holds a `PlugPtr` to communicate with the device.

```
+-----------------+                              +----------------------+
|      IPlug      |                              |  ISocket             |
| (e.g. MyDevice) |                              |  (e.g. VsockStream)  |
+-----------------+                              +----------------------+
| - mSocket:      | --owns (unique_ptr)-->       | - mPlug:             | --refers to (shared_ptr)--> (IPlug)
|   SocketPtr     |                              |   PlugPtr            |
+-----------------+                              +----------------------+
```

This circular reference is carefully broken during teardown to prevent memory leaks.

## 2. The `vsock` Implementation: A Concrete Example

The `vsock.cpp` file provides a concrete implementation of the `cable` interface over virtio-vsock. It introduces the critical locking constraints that motivate the higher-level HAL abstractions.

### Key Components

-   **`GoldfishVirtioVsockDevice`**: A singleton that acts as the "socket manager." It owns all active vsock connections.
-   **`VsockStream`**: The concrete implementation of `ISocket`. It contains the `PlugPtr` to the connected device.
-   **`mStateMutex`**: A `std::recursive_mutex` that protects *all* shared state within the `GoldfishVirtioVsockDevice`, including the map of active streams (`mStreams`).

## 3. The QEMU Threading & Locking Model: The Foundation of Safety

A critical insight for understanding this entire system is that **QEMU's device model is fundamentally single-threaded.** There is one central **QEMU main loop thread** that processes all device I/O, timers, and deferred tasks (Bottom Halves or BHs).

The **Big QEMU Lock (BQL)** is a global mutex that serializes all these operations.

### The Serialization Guarantee

The BQL ensures that only one significant operation can occur at a time on the QEMU thread. This has a profound impact on our design:

-   A **guest-initiated event** (like a disconnect, which triggers `onUnplug`) is handled on the QEMU main loop thread with the BQL held.
-   A **host-initiated task** (like our `close()` task posted to the `QemuEventLoop`) is also executed on the QEMU main loop thread with the BQL held.

Therefore, it is **impossible** for a guest-initiated `onUnplug` to execute concurrently with a host-initiated `close` task. The BQL guarantees that one will fully complete before the other begins.

This simplifies the problem we are solving: we are not managing true multi-threaded data races between the QEMU-side events, but rather a **state management problem** to ensure that whichever teardown path executes first, the second becomes a safe and harmless no-op.

### Lock Hierarchy

The established lock order within the `vsock` implementation is:
1.  **Big QEMU Lock (BQL)** (Implicitly held on entry from QEMU)
2.  **Vsock Lock (`mStateMutex`)** (Explicitly acquired in `vsock.cpp`)

Callbacks on `IPlug` (e.g., `onReceive`, `onUnplug`) are made while the `mStateMutex` is held. This requires `IPlug` implementations to be fast, non-blocking, and careful about re-entrancy.

## 4. The `hal/plug` Abstraction: A Thread-Safe Bridge

The `hal/plug` directory provides a higher-level abstraction designed to solve the problems and constraints imposed by the underlying QEMU/vsock implementation. It allows a HAL to operate on its own `EventLoop` without needing to know about QEMU threads or locks. It achieves this using a pair of matching adapters that marshal calls between the client's thread and the QEMU thread.

### Components

-   **`HalPlug`**: A simplified, thread-safe plug interface for HALs.
-   **`HalSocket`**: A simplified, thread-safe socket interface for HALs.
-   **`HalPlugToIPlugAdapter`**: An `IPlug` that runs on the QEMU thread and posts tasks to the client thread.
-   **`MarshallingHalSocket`**: A `HalSocket` that runs on the client thread and posts tasks to the QEMU thread.

## 5. Final Design: Enforcing a Safe Lifecycle Invariant

Analysis of the teardown sequence revealed several critical flaws, including a deadlock in host-initiated closes and a use-after-free race condition in guest-initiated closes. These issues stemmed from violations of thread ownership and improper synchronization.

The final, robust design solves these problems by focusing on a high-level, developer-centric contract that is enforced by the `HalPlug` itself.

### 5.1. The Core Invariant for HAL Developers

The most important guarantee provided to a HAL developer is the lifecycle of the `HalSocket`:

> **The pointer returned by `HalPlug::socket()` is only valid for use between the start of the `onConnect()` callback and the start of the `onClose()` callback.**

The framework enforces this:
- Before `onConnect()` is called, `socket()` will return a safe, non-functional "null" socket.
- After `onClose()` has been called, `socket()` will also return a "null" socket.

This design prevents crashes from use-after-free or null-pointer-dereference errors and makes the connection lifecycle easy to reason about.

### 5.2. Implementation and Execution Flow

The invariant is enforced by a fully asynchronous, non-blocking setup chain:

1.  **QEMU Thread:** When a guest connects, the `ConnectorRegistry`'s internal factory executes.
    -   It calls the user-provided `HalDeviceFactory` to **create the `HalPlug` object on the QEMU thread.**
    -   It creates the `MarshallingHalSocket`.
    -   It posts a single task to the client's `EventLoop`, capturing both the new `HalPlug` and the `MarshallingHalSocket`.

2.  **Client Thread:** The client's `EventLoop` executes the task.
    -   It calls `establishConnection()` to safely associate the socket with the plug.
    -   It then immediately calls `onConnect()` to notify the user that the connection is live and the `socket()` is now valid.

All subsequent events (`onReceive`, `onClose`) are also marshalled to the client thread. This ensures that all writes to and reads from the `HalPlug`'s internal state, and all user-facing callbacks, are serialized on the client's `EventLoop`.

### 5.3. How This Solves the Flaws

This design elegantly resolves all previously identified issues:

-   **Deadlock:** The use of `postAndWait` in the `ConnectorRegistry` is eliminated, removing the deadlock.
-   **Data Race:** The `HalPlug`'s internal `mSocket` member is now exclusively written to and read from on the client thread. The dangerous cross-thread write from the QEMU thread is gone.
-   **Setup & Teardown Races:** All lifecycle events are serialized as tasks on their respective event loops, and the state is managed by atomic flags, making the entire process robust against races.

### 5.4. The Pragmatic Trade-off: Constructor on QEMU Thread

To achieve this simple, race-free design, one pragmatic trade-off is made:

> **The `HalDeviceFactory` and therefore the `HalPlug`'s constructor are executed on the QEMU main loop thread.**

This is a conscious design choice because it dramatically simplifies the setup logic and eliminates both deadlocks and data races. The contract with the developer is that the `HalPlug` constructor should be lightweight and avoid thread-sensitive operations. All stateful initialization and all other lifecycle events (`onConnect`, `onReceive`, `onClose`) are guaranteed to run on the developer's chosen client thread.

## 6. Future-Proofing: What If the BQL Guarantee Fails?

The entire safety model of the interaction between guest-initiated events (`onUnplug`) and host-initiated tasks (`close()`) **critically depends on the BQL's serialization guarantee.**

This section documents the risks associated with this dependency and the necessary steps to harden the design if the guarantee is ever removed.

### 6.1. The Risk: QEMU's Multi-threaded Evolution

The upstream QEMU project is actively working to replace the single BQL with a more fine-grained locking model to improve performance on multi-core systems. A future version of QEMU that we integrate could adopt this new model for the virtio-vsock backend.

If this happens, our serialization guarantee will be **void**. Guest-initiated events and host-initiated tasks could execute on different threads concurrently, re-introducing the severe use-after-free data races that the BQL currently prevents.

### 6.2. The Solution: A Per-Connection Lifecycle Lock

If the BQL guarantee is lost, we must provide our own serialization. The design would need to be hardened to be truly multi-threaded by introducing a **per-connection lifecycle lock**.

1.  **Introduce a Shared Mutex:** A `std::mutex` would be added to the `VsockStream` (the concrete `ISocket`).
2.  **Share the Mutex:** The `MarshallingHalSocket` and `HalPlugToIPlugAdapter` would both need to hold a `std::shared_ptr<std::mutex>` pointing to the mutex in the `VsockStream`.
3.  **Lock All Critical Sections:** All methods on the adapters that interact with the connection's state or lifetime (`onUnplug`, `onReceive`, the `close()` task, `release()`) would be required to acquire this shared lock.

This "mini-BQL," scoped to a single connection, would re-establish the serialization guarantee and make the design robust in a truly multi-threaded QEMU environment. This change should be considered a **required action** if we ever upgrade to a BQL-free version of QEMU's virtio backend.

## 7. Alternatives Considered: RAII-Based Lifetime

An alternative, more C++-idiomatic design was considered that would tie the connection lifetime directly to the `HalPlug` object's lifetime using RAII principles.

### 7.1. The RAII Model Proposal

-   The `HalPlug` interface would be simplified to remove the `onConnect()` and `onClose()` methods.
-   The `HalDeviceFactory` would return a factory function (e.g., `[] { return std::make_shared<MyPlug>(); }`).
-   The framework would invoke this factory at connection time. The `HalPlug`'s **constructor** would serve the role of `onConnect`.
-   The framework would release its `std::shared_ptr` to the `HalPlug` at disconnection time. The `HalPlug`'s **destructor** would serve the role of `onClose`.

### 7.2. Analysis and Rejection

While appealing for its simplicity and adherence to RAII, this model was rejected due to critical complexities that arise from our asynchronous, cross-thread environment.

-   **The Asynchronous Destructor Problem:** This is the most significant flaw. A C++ destructor is expected to be synchronous and complete its work before returning. However, our `close()` operation is asynchronous (it posts a task to the QEMU thread). A destructor cannot safely block waiting for this task to complete without re-introducing the deadlocks we worked to eliminate. It also cannot safely return without blocking, as that would create a race condition where the client's `EventLoop` could be destroyed before the QEMU thread has finished its cleanup, leading to crashes. Asynchronous cleanup in destructors is a notoriously difficult problem.

-   **Handling of Pre-Connection Data:** As discovered, the underlying `cable` protocol can deliver an `onReceive` event with configuration data *before* the `onConnect` event. In the RAII model, the `HalPlug` object would not yet exist when this data arrives, forcing the framework to implement a complex and fragile buffering mechanism.

**Conclusion:** The explicit, callback-based lifecycle (`onConnect`/`onClose`) of the current design, while less purely RAII, is a more robust and safer pattern for this specific concurrent environment. It correctly handles all known protocol edge cases and avoids the dangerous complexities of asynchronous destruction.
