# Component: gRPC Async Utils

**Role:** High-level C++ templates and adapters for gRPC's asynchronous (callback-based) API.
**Location:** `hardware/generic/goldfish/emulator/grpc/async`
**Namespace:** Template-based (mixins)

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `WithSimpleReader<T>` | `:async` | `.../simple_async_grpc.h` | Mixin for automatic stream reading in reactors. |
| `WithSimpleQueueWriter<T>` | `:async` | `.../simple_async_grpc.h` | Mixin for thread-safe queued writes in reactors. |
| `SyncToAsyncBidiAdapter<R, W>` | `:async` | `.../sync_to_async_adapter.h` | Bridge to use async logic over sync gRPC streams. |

## Critical Infrastructure
* **Reactor Mixins:** `WithSimpleReader` and `WithSimpleQueueWriter` simplify the state machine management required for gRPC's callback API. They handle the "StartRead/OnReadDone" and "StartWrite/OnWriteDone" loops automatically.
* **Lambda Adapters:** `SimpleServerLambdaReader` allows passing a C++ lambda to handle incoming stream messages without subclassing a full reactor.
* **Threading Workaround:** `SyncToAsyncBidiAdapter` provides a stable implementation for bi-directional streams on platforms (like Windows) where gRPC's reactor implementation might have stability issues, by spawning dedicated reader/writer threads.

## Dependencies
* **External:** `@grpc//:grpc++`.

## Threading Model
* **gRPC Thread Pool:** Callbacks (`OnReadDone`, `OnWriteDone`) typically execute on gRPC's internal completion queue threads.
* **Queued Writing:** `WithSimpleQueueWriter` uses a internal mutex and queue to allow `Write()` to be called from any thread safely.
