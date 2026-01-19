# Component: gRPC Event Stream Support

**Role:** Bridges internal emulator event sources to gRPC server-to-client streams.
**Location:** `hardware/generic/goldfish/emulator/grpc/event-stream`
**Namespace:** `android::emulation::control`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `GenericEventStreamWriter<T>` | `:event-stream` | `.../grpc_event_stream_support.h` | Bridges a `CallbackEventSource` to a gRPC stream. |
| `UniqueEventStreamWriter<T>` | `:event-stream` | `.../grpc_event_stream_support.h` | Filters out duplicate consecutive events before streaming. |

## Critical Infrastructure
* **Event Bridging:** Automatically handles the subscription lifecycle. It registers a callback with the `CallbackEventSource` on construction and unregisters on cancellation/done.
* **Stream Writing:** Uses `SimpleServerWriter` (from `emulator/grpc/async`) to handle the asynchronous gRPC write queue.
* **Deduplication:** `UniqueEventStreamWriter` uses Protobuf's `MessageDifferencer` to ensure only state *changes* are pushed to the client, reducing bandwidth for high-frequency spurious updates.

## Dependencies
* **Core:** `//emulator/grpc/async` (SimpleServerWriter).
* **Events:** `@aemu//base:event-support` (CallbackEventSource).
* **External:** `@grpc//:grpc++`, `google::protobuf`.

## Threading Model
* **Event Source Thread:** `eventArrived` is called on whichever thread the event source fires (e.g., UI thread, Render thread).
* **Marshalling:** Events are immediately pushed into the `SimpleServerWriter` queue, which marshals them to the gRPC completion queue thread pool.
