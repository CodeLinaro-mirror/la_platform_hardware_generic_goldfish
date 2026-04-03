# Component: gRPC Interceptors

**Role:** Provides cross-cutting concerns for gRPC services, including logging, lifecycle management, and crash diagnostics.
**Location:** `hardware/generic/goldfish/emulator/grpc/interceptors`
**Namespace:** `android::control::interceptor`

## Integration Guide
| Class / Interface | Bazel Target | Header Path | Description |
| :--- | :--- | :--- | :--- |
| `LoggingInterceptor` | `:logging_interceptor` | `.../logging_interceptor.h` | Records RPC method, latency, and status. |
| `MetricsInterceptor` | `:metrics_interceptor` | `.../metrics_interceptor.h` | Collects and reports gRPC usage metrics to Studio. |
| `IdleInterceptor` | `:idle_interceptor` | `.../idle_interceptor.h` | Shuts down emulator after period of inactivity. |
| `BreadcrumbInterceptor` | `:breadcrumb_interceptor` | `.../breadcrumb_interceptor.h` | Adds RPC state to crash report annotations. |

## Critical Infrastructure
* **Observability:**
    * `LoggingInterceptor` generates `InvocationRecord` structs. `StdOutLoggingInterceptorFactory` provides a default implementation that writes to `LOG(INFO)`.
    * `MetricsInterceptor` leverages `LoggingInterceptor` to collect RPC statistics (bytes, messages, duration) and reports them via `goldfish::metrics::MetricsReporter`.
* **Lifecycle:** `IdleInterceptor` uses a `goldfish::async::EventLoop::Timer` to periodically check if any RPCs were active. If the "Termination Time" is exceeded, it triggers an orderly shutdown.
* **Diagnostics:** `BreadcrumbInterceptor` records RPC lifecycle events (Start, Phase, Message, Status) into a high-performance `ProtoCircularLog<GrpcBreadcrumb>`. This log is backed by a `BinaryAnnotation`, ensuring structured forensic data is captured directly in Crashpad minidumps.

## Dependencies
* **Core:** `//emulator/libs/async` (EventLoop), `//emulator/libs/debug`, `//emulator/libs/proto_data_store` (`ProtoCircularLog`).
* **External:** `@grpc//:grpc++`, `@aemu//protos/services/diagnostic:grpc_diagnostic_cc_proto`.

## Threading Model
* **Thread Safe:** Interceptors are invoked by the gRPC stack on its completion queue threads. The `BreadcrumbInterceptor` uses a mutex-protected circular buffer to ensure thread-safe logging with minimal contention. The `IdleInterceptor` uses atomic counters to track active requests across threads.
